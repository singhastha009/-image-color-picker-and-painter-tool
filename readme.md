

Compile the code - gcc -o a5 a5.c $(pkg-config --cflags --libs gtk4)
Run - ./a5 scenery.png 

Features- 
Pick Colors: Click anywhere on the image to get the RGB color of that pixel.

Paint Mode: Enable painting to draw on the image using a brush. So to use the paint button, click on it, select the brush size and go to get color and then start painting.

Brush Size: Adjust the brush size for finer or broader strokes. The line below selects the brush size

Undo/Redo: Revert or redo painting actions.

Save Image: Save the modified image as painted_image.png in the same directory.

An example of the painted_image has also been shown which was saved.
