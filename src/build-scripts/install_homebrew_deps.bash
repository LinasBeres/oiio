#!/bin/bash

# This script, which assumes it is runnign on a Mac OSX with Homebrew
# installed, does a "brew install" in all packages reasonably needed by
# OIIO.

if [ `uname` != "Darwin" ] ; then
    echo "Don't run this script unless you are on Mac OSX"
    exit 1
fi

if [ `which brew` == "" ] ; then
    echo "You need to install Homebrew before running this script."
    echo "See http://brew.sh"
    exit 1
fi

brew update >/dev/null
echo ""
echo "Before my brew installs:"
brew list --versions
brew install ccache cmake
brew install ilmbase openexr
brew install opencolorio
brew install freetype
#brew install libraw
brew install libpng webp jpeg-turbo
brew install openjpeg
brew install dcmtk
brew install qt
brew install field3d
brew install ffmpeg
brew install opencv
brew upgrade python
brew install pybind11 numpy
brew link --overwrite numpy
if [ "$LINKSTATIC" == "1" ] ; then
    brew install little-cms2 tinyxml szip
    brew install homebrew/dupes/bzip2
    brew install yaml-cpp --with-static-lib
fi
echo ""
echo "After brew installs:"
brew list --versions
