# `exec_path_args`

## License

![LGPLv3 image](doc/lgplv3-with-text-154x68.png)

[`LGPLv3`](https://www.gnu.org/licenses/lgpl-3.0.html) -> [COPYING](COPYING) & [COPYING.lesser](COPYING.LESSER)

## Running unit tests with sanitizers

Examples:

### With `tsan`

```bash
$ scripts/configure.bash -DEXECPATHARGS_ASAN=OFF -DEXECPATHARGS_MSAN=OFF -DEXECPATHARGS_UBSAN=OFF -DEXECPATHARGS_TSAN=ON && scripts/unit_tests.bash
...
```

### With `asan` & `ubsan`

```bash
$ scripts/configure.bash -DEXECPATHARGS_ASAN=ON -DEXECPATHARGS_MSAN=OFF -DEXECPATHARGS_UBSAN=ON -DEXECPATHARGS_TSAN=OFF && scripts/unit_tests.bash
...
```

## TODO

- Documentation?
- General cleanup
  - Consider renaming used namespace, class, etc.
  - [CMakeLists.txt](CMakeLists.txt) related
    - Polish it for now
    - Add presets for sanitizers
    - Compile it with varisous warnings, etc.
- Maybe not so "aggressive" tag name(s)?
  - E.g. not `v000.001.000` but `v0.1.0`
