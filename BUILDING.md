### For Mac

```sh
# This is just for devel.
# Currently on OSX El Capitan 10.11.5
git clone --recursive https://github.com/pathway27/bookr-mod-vita
# install ext/SOIL (there's a readme)
brew install glfw GLEW glm
make -f Makefile.macglfw
./bookr.glut
```

### For Windows

```
git clone --recursive https://github.com/pathway27/bookr-mod-vita
Open the solution file in Visual Studio
Download lots of dependancies into (ext), add to visual studio settings.
Change to Folder View and run to make bookr-mod-vita.exe
```
