# Dockerfile

For building chelper and MIPS firmware, best **not** to use it for building anything else.

## Publishing docker file

```
docker build . -t pellcorp/k1-klipper-fw-build
docker login
docker push pellcorp/k1-klipper-fw-build
```
