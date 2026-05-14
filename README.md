# `exec_path_args`

## License

![LGPLv3 image](doc/lgplv3-with-text-154x68.png)

[`LGPLv3`](https://www.gnu.org/licenses/lgpl-3.0.html) -> [COPYING](COPYING) & [COPYING.lesser](COPYING.LESSER)

## Running unit tests with sanitizers

Examples:

### With `tsan`

```bash
$ scripts/configure.bash --tsan && scripts/unit_tests.bash
...
```

### With `asan` & `ubsan`

```bash
$ scripts/configure.bash --asan && scripts/unit_tests.bash
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
