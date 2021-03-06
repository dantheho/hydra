#include "build-result.hh"
#include "store-api.hh"
#include "misc.hh"
#include "util.hh"
#include "regex.hh"

using namespace nix;


static std::tuple<bool, string> secureRead(Path fileName)
{
    auto fail = std::make_tuple(false, "");

    if (!pathExists(fileName)) return fail;

    try {
        /* For security, resolve symlinks. */
        fileName = canonPath(fileName, true);
        if (!isInStore(fileName)) return fail;
        return std::make_tuple(true, readFile(fileName));
    } catch (Error & e) { return fail; }
}


BuildOutput getBuildOutput(std::shared_ptr<StoreAPI> store, const Derivation & drv)
{
    BuildOutput res;

    /* Compute the closure size. */
    PathSet outputs;
    for (auto & output : drv.outputs)
        outputs.insert(output.second.path);
    PathSet closure;
    for (auto & output : outputs)
        computeFSClosure(*store, output, closure);
    for (auto & path : closure) {
        auto info = store->queryPathInfo(path);
        res.closureSize += info.narSize;
        if (outputs.find(path) != outputs.end()) res.size += info.narSize;
    }

    /* Get build products. */
    bool explicitProducts = false;

    Regex regex(
        "(([a-zA-Z0-9_-]+)" // type (e.g. "doc")
        "[[:space:]]+"
        "([a-zA-Z0-9_-]+)" // subtype (e.g. "readme")
        "[[:space:]]+"
        "(\"[^\"]+\"|[^[:space:]\"]+))" // path (may be quoted)
        "([[:space:]]+([^[:space:]]+))?" // entry point
        , true);

    for (auto & output : outputs) {
        Path failedFile = output + "/nix-support/failed";
        if (pathExists(failedFile)) res.failed = true;

        auto file = secureRead(output + "/nix-support/hydra-build-products");
        if (!std::get<0>(file)) continue;

        explicitProducts = true;

        for (auto & line : tokenizeString<Strings>(std::get<1>(file), "\n")) {
            BuildProduct product;

            Regex::Subs subs;
            if (!regex.matches(line, subs)) continue;

            product.type = subs[1];
            product.subtype = subs[2];
            product.path = subs[3][0] == '"' ? string(subs[3], 1, subs[3].size() - 2) : subs[3];
            product.defaultPath = subs[5];

            /* Ensure that the path exists and points into the Nix
               store. */
            if (product.path == "" || product.path[0] != '/') continue;
            try {
                product.path = canonPath(product.path, true);
            } catch (Error & e) { continue; }
            if (!isInStore(product.path) || !pathExists(product.path)) continue;

            /*  FIXME: check that the path is in the input closure
                of the build? */

            product.name = product.path == output ? "" : baseNameOf(product.path);

            struct stat st;
            if (stat(product.path.c_str(), &st))
                throw SysError(format("getting status of ‘%1%’") % product.path);

            if (S_ISREG(st.st_mode)) {
                product.isRegular = true;
                product.fileSize = st.st_size;
                product.sha1hash = hashFile(htSHA1, product.path);
                product.sha256hash = hashFile(htSHA256, product.path);
            }

            res.products.push_back(product);
        }
    }

    /* If no build products were explicitly declared, then add all
       outputs as a product of type "nix-build". */
    if (!explicitProducts) {
        for (auto & output : drv.outputs) {
            BuildProduct product;
            product.path = output.second.path;
            product.type = "nix-build";
            product.subtype = output.first == "out" ? "" : output.first;
            product.name = storePathToName(product.path);

            struct stat st;
            if (stat(product.path.c_str(), &st))
                throw SysError(format("getting status of ‘%1%’") % product.path);
            if (S_ISDIR(st.st_mode))
                res.products.push_back(product);
        }
    }

    /* Get the release name from $output/nix-support/hydra-release-name. */
    for (auto & output : outputs) {
        Path p = output + "/nix-support/hydra-release-name";
        if (!pathExists(p)) continue;
        try {
            res.releaseName = trim(readFile(p));
        } catch (Error & e) { continue; }
        // FIXME: validate release name
    }

    /* Get metrics. */
    for (auto & output : outputs) {
        auto file = secureRead(output + "/nix-support/hydra-metrics");
        for (auto & line : tokenizeString<Strings>(std::get<1>(file), "\n")) {
            auto fields = tokenizeString<std::vector<std::string>>(line);
            if (fields.size() < 2) continue;
            BuildMetric metric;
            metric.name = fields[0]; // FIXME: validate
            metric.value = atof(fields[1].c_str()); // FIXME
            metric.unit = fields.size() >= 3 ? fields[2] : "";
            res.metrics[metric.name] = metric;
        }
    }

    return res;
}
