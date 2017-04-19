# vapoursynth-autocrop
AutoCrop for Vapoursynth  

# Usage  
All in One  
```acrop.AutoCrop(clip, range, top, bottom, left, right, color, color_second)```  
Search and crop borders with matching color ... Output is a clip with varying dimensions  

Search only  
```acrop.CropValues(clip, range, top, bottom, left, right, color, color_second)```  
Write crop values into frame props ...  

Crop only  
```acrop.CropProp(clip)```  
Crop the Clip by the values given by acrop.CropValues ...  Output is a clip with varying dimensions  

# Commands  
Optional:  
range, top, bottom, left, right, color, color_second  

Default:  
range = 4  
top, bottom, left, right = range  
color = {0,123,123}  
color_second = {21,133,133}  
  
# Thanks  
kageru, Attila, stux!, and Myrsloik 

# Work in Progress  
