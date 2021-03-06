# Rofi Top - interactive process viewer

A plugin for rofi that emulates top behaviour.

To run this you need an up to date checkout of rofi git installed.

Run rofi like:

```bash
    rofi -show top -modi top 
```

## Screenshot

![rofi top](./rofi-top.png)

## Keybindings

The following keybindings change sorting:

| Keybinding | Sort item      |
|------------|----------------|
| `custom-1` | Memory         |
| `custom-2` | Pid            |
| `custom-3` | CPU Time       |
| `custom-4` | Name           |
| `custom-5` | CPU Percentage |

Repeatingly selecting the entry will change sort order.

The current setting is not stored between consecutive runs.
## Compilation

### Dependencies

| Dependency | Version |
|------------|---------|
| libgtop    | 2.34    |
| rofi 	     | 1.4	   |

### Installation

**Rofi-top** uses autotools as build system. If installing from git, the following steps should install it:

```bash
$ autoreconf -i
$ mkdir build
$ cd build/
$ ../configure
$ make
$ make install
```
