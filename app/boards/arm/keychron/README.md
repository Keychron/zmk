# Keychron `NRF52840` Experimental Support



### Specifications:

 - `NRF52840`  
 - 15 column x 5 row key matrix with a ROW -> COL (`row2col`) layout
 - DIP switch embedded in matrix at `(4,4)`
 - LED Matrix
 - 2x I2C `CKLED2001` LED Drivers
 - EC11 Encoder

### Current Progress:

### Flashing:



### Development:

1. Follow the [ZMK Documentation](https://zmk.dev/docs/development/setup) to set up your local build environment
2. To build the firmware, run:
```sh
west build -p -b keychron
```
3. To build and flash the the firmware, hold down the RESET button while connecting the keyboard over USB. The run:
```sh
west build -p -b keychron && west flash
```


