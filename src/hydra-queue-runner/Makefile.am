bin_PROGRAMS = hydra-queue-runner

hydra_queue_runner_SOURCES = hydra-queue-runner.cc queue-monitor.cc dispatcher.cc \
 builder.cc build-result.cc build-remote.cc  \
 build-result.hh counter.hh pool.hh sync.hh token-server.hh state.hh db.hh
hydra_queue_runner_LDADD = $(NIX_LIBS) -lpqxx

AM_CXXFLAGS = $(NIX_CFLAGS) -Wall
