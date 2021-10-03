# Geiger Display
Software to enhance a Geiger Counter kit with a display.

Currently adapted for a TTGO-Display.

Ready for use in PlatformIO (PIO) [https://platformio.org/](https://platformio.org/).

Functionality:
 - Simple pin input, interrupt driven
 - CPM (Counts per Minute) or µS/h (Micro Sieverts per hour)
 - Display a graph of previous counts
 - Log gauge CPM or µS/h and average / max
 - Average- or maximum CPM or µS/h
 - 4 update speeds: Very fast (5 sec), Fast (15 sec),  Normal (30 sec), Slow (1 min), Very Slow (2 min)

## Geiger counter kit
![Geiger Kit](assets/RadiationD-v1.1.jpg)


## Example images of display
The images are over exposed because of the LCD. However, to give an impression:

### Startup screen
As this is no commercial or calibrated product, Krusty is there to remember you each time you start!
![Krusty geiger counter](assets/20211004_011638.jpg)

### Time graph including AVG or MAX
![Example time graph](assets/20211004_011144.jpg)
### Logarithmic scale of the actual level
 There are other, identical looking, screens (avg and max)
![Example uS/hr](assets/20211004_002440.jpg)

### Logarithmic scale of the Counts Per Minute (CPM)
Also here, there are other, identical looking, screens (avg and max)
![Example CPM](assets/20211004_003752.jpg)

### Total exposure
Since startup.
![Example Total exposure](assets/20211004_002539.jpg)

# Example image of setting the update speed to slow
![Setting slow updates](assets/20211004_004235.jpg)

