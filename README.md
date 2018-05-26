# vapoursynth-autocrop
AutoCrop for Vapoursynth  

# Usage  
AutoCrop  
```python
acrop.AutoCrop(clip src, int range=4, int top=range, int bottom=range, int left=range, int right=range, int[] color=[0,123,123], int[] color_second=[21,133,133])
```  

Search only  
```python
acrop.CropValues(clip src, int range=4, int top=range, int bottom=range, int left=range, int right=range, int[] color=[0,123,123], int[] color_second=[21,133,133])
```

## Compilation

### Linux
```
g++ -std=c++11 -shared -fPIC -O2 autocrop.cpp -o libautocrop.so
```

### Cross-compilation for Windows
```
x86_64-w64-mingw32-g++ -std=c++11 -shared -fPIC -O2 autocrop.cpp -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic -s -o autocrop.dll
```


# Thanks  
kageru, Attila, stux!, and Myrsloik
