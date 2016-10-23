TypeSan quick setup guide
=========================

Installation
------------

For this manual, we assume that you are running Ubuntu Server 16.04 LTS.
Other distributions or Ubuntu versions might require different packages
to be able to perform the setup.

We also assume that you have a copy of SPEC CPU2006 installed in $HOME/cpu2006.
If your SPEC install is located somewhere else, adjust the value of PATHSPEC
accordingly. If you do not have a copy of SPEC CPU2006, set the environment
variable BUILD_SPEC to 0.

If you want to build Firefox, you will need to install the Firefox build
prerequisites beforehand. Instructions are found here:

https://developer.mozilla.org/en-US/docs/Mozilla/Developer_guide/Build_Instructions/Linux_Prerequisites

If you do not want to build Firefox, set BUILD_FIREFOX=0.

Follow the following instructions:

    sudo apt-get install autoconf bison build-essential git libtool libtool-bin
    git clone ssh://git@git.cs.vu.nl:1337/i.haller/TypeSanOpenSource.git
    cd TypeSanOpenSource
    PATHSPEC="$HOME/cpu2006" ./autosetup.sh

This sets up TypeSan and instruments all C++ SPEC CPU2006 benchmarks
as well as Firefox using all supported configurations:

* default - default compilation without tcmalloc
* baseline - default compilation with uninstrumented tcmalloc
* typesan - typesan instrumented compilation
* typesanbl - typesan instrumented compilation with blacklist
* typesanresid - residual typesan instrumented compilation


Microbenchmarks
---------------

To run the microbenchmarks, type:

    ./run-ubench-typesan.sh

'typesan' can be replaced with any of the other configurations.


Firefox
-------

To run Firefox, type:

    ./run-firefox-typesan.sh

'typesan' can be replaced with any of the other configurations.

Please note that Firefox requires a graphical interface, which Ubuntu Server
does not offer by default. Ensure access to an X11 server and export
the correct value of $DISPLAY before running this script.


SPEC CPU2006
------------

To run the microbenchmarks, type:

    ./run-spec-typesan.sh benchmarks

'typesan' can be replaced with any of the other configurations.

'benchmarks' should be set to one or more of the following:

* 444.namd
* 447.dealII
* 450.soplex
* 453.povray
* 471.omnetpp
* 473.astar
* 483.xalancbmk
