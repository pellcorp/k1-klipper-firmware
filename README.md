This is a fork of kalico for building Simple AF fw/K1/ nozzle, bed and mcu bin files only

Build with:

```
docker run -ti -v $PWD:$PWD pellcorp/k1-klipper-fw-build $PWD/build.sh
```

Or build a specific firmware version only with, in this case we are building the nozzle mcu for CR10SE, you can find
the predefined build files in the root of this repo, they are .config.XXX, where XXX is cr10se-noz in this example:

```
docker run -ti -v $PWD:$PWD pellcorp/k1-klipper-fw-build $PWD/_build.sh cr10se-noz
```

You will find the bin in the outfw/cr10se directory
