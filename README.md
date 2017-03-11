A plugin for rofi that emulates top behaviour 

To run this you need an up to date checkout of rofi git installed.

Run rofi like:

```bash
    rofi -show top -modi top.so 
```

## Keybindings

The following keybindings change sorting:

 * `custom-1`: Memory
 * `custom-2`: Pid
 * `custom-3`: CPU Time 
 * `custom-4`: Name
 * `custom-5`: CPU Percentage

Repeatingly selecting the entry will change sort order.


## Compilation

### Dependencies

 * libgtop 2.34
 * rofi 1.4 or up.

### Installation

```bash
$ autoreconf -i
$ mkdir build
$ cd build/
$ ../configure
$ make
$ make install
```
