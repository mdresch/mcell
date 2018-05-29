# MCell

MCell (Monte Carlo Cell) development is supported by the NIGMS-funded
(P41GM103712) National Center for Multiscale Modeling of Biological Systems
(MMBioS).

MCell is a program that uses spatially realistic 3D cellular models and
specialized Monte Carlo algorithms to simulate the movements and reactions of
molecules within and between cells—cellular microphysiology. 

[![Build Status](https://travis-ci.org/mcellteam/mcell.svg?branch=master)](https://travis-ci.org/mcellteam/mcell)
[![Build status](https://ci.appveyor.com/api/projects/status/github/mcellteam/mcell?branch=master&svg=true)](https://ci.appveyor.com/project/jczech/mcell/branch/master)
<a href="https://scan.coverity.com/projects/mcellteam-mcell">
  <img alt="Coverity Scan Build Status"
       src="https://scan.coverity.com/projects/8521/badge.svg"/>
</a>

## Download Latest Test Builds

These builds are the from the head of this branch and are not guaranteed to be
stable. Use at your own risk.

* [Linux](https://bintray.com/jczech/mcell/download_file?file_path=mcell-linux-gcc.tgz)
* [OSX](https://bintray.com/jczech/mcell/download_file?file_path=mcell-osx-gcc.tgz)

## Build Requirements:

### Ubuntu 16.04:

Run the following commands:

    sudo apt-get update
    sudo apt-get install cmake build-essential bison flex python3-dev swig libboost-all-dev

## Building MCell Executable from Source:

### CMake

If this is your first time cloning the repo, you'll want to do this first:

    git submodule init
    git submodule update

To build MCell and pyMCell for Macs or Linux, run the following commands from
the main mcell directory:

    mkdir build
    cd build
    cmake ..
    make

See the [Windows
Development](https://github.com/mcellteam/mcell/wiki/Windows-Development) page
on the github wiki for information about building MCell on Windows.

## Alternative (non-CMake) Method to Build pyMCell:

PyMCell is an experimental MCell-Python library. You can build it using the
traditional CMake method above or this distutils based method, which requires
swig and a newer version of Python 3 (preferably 3.5 or greater). Run the
following command:

  python3 setupy.py build

## How to Test:

[nutmeg](https://github.com/mcellteam/nutmeg) is a regression test
framework for MCell. Installation and usage instructions are listed on the
nutmeg project page.
