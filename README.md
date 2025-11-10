# XLR8 Extensible Runtime and Runtime

XLR8 is a successor to XL intended to be usable to build a distributed,
[hardware-agnostic][taos], [actor-based][art] [operating system][os].

The big picture of the components we want to be able to write with XLR8 is shown
below:

![Overall OS design](OS Design.png)

[os]: https://grenouillebouillie.wordpress.com/2020/03/27/old-ideas-about-an-os-from-scratch/
[taos]: http://uruk.org/emu/Taos.html
[art]: https://fosdem.org/2024/events/attachments/fosdem-2024-3485-hactorscript-in-art-bug-free-software-on-unhackable-hardware/slides/22293/Draft2_3QvqGJE.pdf

The first step is to write the compiler and bootstrap it.


## Key ideas

The key ideas of XLR8 are:

1. A sandboxed runtime
