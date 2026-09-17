## Building

```sh
make             # Build all tests
make vertex-skip # Build one test
make clean       # Remove build outputs
```

The Makefile uses devkitPPC/libogc when `DEVKITPPC` and `DEVKITPRO` are configured.
Otherwise it runs the build through Docker using `devkitpro/devkitppc:latest`.

## Z-freeze

Found in:
* Mario Golf: Toadstool Tour
* Mario Power Tennis (the Plaza map)

Expected:

- **Left (Z-freeze off):** solid blue rectangle.
- **Right (Z-freeze on):** blue rectangle with a red rectangle visible inside it.

If both sides are solid blue, Z-freeze is broken or not implemented.

![](z-freeze/expected.png)

[FIFO capture](z-freeze/z-freeze.dff)

## Vertex-skip

Found in:
* Mario Golf: Toadstool Tour

Expected: three identical solid blue rectangles, from left to right:

- **Reference:** four direct vertices forming a triangle strip.
- **Index8:** the same strip with position index `0xff` inserted in the middle.
- **Index16:** the same strip with position index `0xffff` inserted in the middle.

![](vertex-skip/expected.png)

[FIFO capture](vertex-skip/vertex-skip.dff)
