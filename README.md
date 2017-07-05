[![Gitter](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/nem0/LumixEngine?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge)
[![License](http://img.shields.io/:license-mit-blue.svg)](http://doge.mit-license.org)

# OpenFBX

Lightweight open source FBX importer. Used in [Lumix Engine](https://github.com/nem0/lumixengine). It's not a full-featured importer, but it suits all my needs. It can load geometry (with uvs, normals, tangents, colors), skeletons, animations, materials and textures. 

Feel free to request new features. I will eventually try to add all missing fbx fatures.

## Compile demo project

1. download source code
2. execute [projects/genie_vs15.bat](https://github.com/nem0/OpenFBX/blob/master/projects/genie_vs15.bat)
3. open projects/tmp/vs2015/OpenFBX.sln in Visual Studio 2015
4. compile and run

Demo is windows only. Library is multiplatform.

## Use the library in your own project

1. add files from src to your project
2. use

See [demo](https://github.com/nem0/OpenFBX/blob/master/demo/main.cpp#L203) as an example how to use the library.
