# Common benchmarking standard

This offers a standardized way for applications to offer test automation features to users.

There are three target groups for this initiative:

* People doing functional testing. These may be anything from people working for hardware vendors to tools vendors or developers themselves wanting to do automated testing.
* People doing performance testing.  This could be end users who want to know the performance of their own system, Youtube reviewers, tech journalists, or people working for hardware vendors. Basically anyone trying to get reliable data about the performance of a particular piece of content.
* People trying to debug some application issue. This could be end users or vendors, typically trying to go through options systematically to try to narrow down what it is that is causing whatever issue they are seeing.

We hope that applications by adopting parts of this standard can improve the software ecosystem as a whole by making it easier to conduct automated testing and quality reviews.

This standard provides three things:

* A ```capabilities file``` in JSON format that is bundled with the application in a platform-specific location and describes what standard features it offers.
* An ```enable file``` in JSON format that is provided by the user to the application.
* A platform-specific way that the benchmark mode is enabled and the location of this enable file is provided to the application.

If the benchmark mode is enabled, the application shall relax restrictions on the environment it is running on and instead assume it is not to be trusted and adapt its available features as appropriate (for example turn off ranked multiplayer).

The minimum standards implementation is to provide an empty (ie "{}") JSON capabilities file, to listen for the platform-specific activation signal, turn off any environment restrictions if this signal is given, and write out a results JSON. Everything else is optional.

## Capabilities file

The capabilities file is a JSON that the application bundles to describe which standard features it is offering and how it can be used.

### Specification

Platform independent JSON description. Example:

```json
    {
        "description": "free form optional description of the app",
        "scenes": {
            "example": {
                "description": "free form human readable description of the scene",
                "run_time": 10
            }
        },
        "settings": {
            "example": {
                "description": "free form human readable description of the option",
                "type": "selection",
                "options": [ "high performance", "balanced", "high quality" ]
            }
        },
        "capabilities": {
            "non_interactive": "always"
        },
        "adaptations": {
            "example": "free form human readable description of the adaptation"
        }
    }
```

The defined top-level fields are as follows:

| Field | Mandatory | Description |
| ----- | --------- | ----------- |
| description | No | A short human readable description of the content |
| std_version | No | Version of this standard that is supported. The default value is 1. |
| scenes | No | Dictionary describing the scenes found in the content. |
| settings | No | Dictionary describing application-defined settings that are available. |
| capabilities | No | Dictionary describing standard-defined capabilities that are available. |
| adaptations | No | Dictionary describing specific application-defined dynamic adaptations or workarounds that are normally enabled by runtime heuristics. |

#### Scenes

Available fields that can be added in each scene entry:

| Field | Mandatory | Description |
| ----- | --------- | ----------- |
| name | Yes | A name for the scene that can later be used to activate it |
| description | No | A human readable description of the scene |
| run_time    | No  | If not run in fixed framerate mode and this is known, how many seconds the scene will take to run. |
| run_frames  | No  | If in fixed framerate mode, how many frames the test will run. If there are multiple windows, the sum total of each of them. |

#### Settings

Available fields that can be added in each settings entry:

| Field | Mandatory | Description |
| ----- | --------- | ----------- |
| description | Yes | A short human readable description of the option |
| type | Yes | One of "selection", "bool", or "number" |
| options | If "selection" in type | If type is "selection", this must be an array of possible values for the setting |
| min | If "number" in type | If type is "number", this must be the minimum value for this setting |
| max | If "number" in type | If type is "number", this must be the maximum value for this setting |

#### Capabilities

The capabilities entry is a list of key-value pairs, where the key is one of those described in the table below, and the value is either a setting enum type or a boolean, as indicated below. In the enable file it takes a value as described below.

|Capability | Required capability value | Enable value type | Enable values allowed | Description |
| --------- | ------------------------- | ----------------- | --------------------- | ----------- |
| non_interactive | enum | bool | true | The application is capable to run in fully non-interactive mode. If enabled, it must automatically run the selected scene (or some default scene if none given or scene selection not supported), and then if able to run it to completion, exit with a successful exit code. If it fails to complete the scene, it shall exit with a failure exit code. |
| fixed_framerate | true | number | zero or higher | The application is capable of running in a fixed framerate mode. This means that no matter the performance it will render the same content in the same number of frames every time. If the enable value is set to a non-zero value, this is the number of desired milliseconds of simulated rendering time between each frame. If zero, fixed framerate mode is disabled. |
| no_cpu-performance_adaptations | enum | bool | true | The application is capable of disabling dynamic runtime CPU adaptations to improve performance. This also include using settings that are adapted to application prior knowledge of the device performance and core pinning, but not sizing thread pools according to CPU core numbers. TBD: Split this up? |
| no_gpu_performance_adaptations | enum | bool | true | The application is capable of disabling dynamic runtime graphics adaptations to improve performance. This also include using settings that are adapted to application prior knowledge of the device performance. |
| no_vendor_performance_adaptations | enum | bool | true | The application is capable of disabling vendor specific adaptations to improve performance. If specific adaptations are exposed in the capabilities file and are enabled in the enable file, those adaptations should override changes made by this option. |
| no_vendor_workarounds	| enum | bool | true | The application is capable of disabling vendor specific bug workarounds that have been added to prevent rendering artifacts or crashes. If specific adaptations are exposed in the capabilities file and are enabled in the enable file, those adaptations should override changes made by this option. |
| no_os_workarounds | enum | bool | true | As above, but for workarounds that have been added to prevent rendering artifacts or crashes on specific versions of an operating or windowing system version system. |
| no_loading_screen | enum | bool | true | The application is capable of turning off any loading screen that is shown while the application is starting up or changing scenes. Instead, no new frames should be rendered. |
| visual_settings | true | number | zero or higher | The application is capable of tuning its rendering quality by giving it a 1-100 value, where 1 is lowest and 100 is highest. An enable value of zero means this capability is not to be used. |
| loops | true | number | zero or higher | The application is capable of running its scene in a loop with no or only a minimal scene loading between each loop iteration. The enable value gives the number of loops to be run, where zero means run in an infinite loop. This is useful in particular for measuring sustained performance, or measuring power, battery and temperature. |
| gpu_delay_reuse | enum | number | zero or higher | The application is capable of delaying the reuse of GPU resources. The enable value gives the number of frames that resources must not be reused for, or zero if this capability should not be enabled. |
| gpu_no_coherent | enum | bool | true | The application is capable of behaving as if no coherent memory exists, and must explicitly call the graphics API to flush any modified memory before it is to be used for rendering. This allows for instance gfxreconstruct to run in 'assisted' tracing mode instead of using guard pages which may speed up runtime performance while tracing or avoid issues with guard pages. |
| gpu_frame_deterministic | enum | true | true | Whether the application generates a deterministic final rendering output. |
| gpu_fully_deterministic | enum | true | true | Whether the application generates deterministic rendering outputs from every GPU compute or rendering step. |

### Adaptations

The adaptations entry is a list of key-value pairs, where the key is an application-defined name, and the value is a free-form description of what it does.

An adaptation is a behaviour that may be enabled by the application depending on runtime heuristics. The user may use the enable file to force this adaptation on or off, bypassing the runtime heuristics.

### Setting enum type

The setting enum type is a string with one of the values "always", "option", "opt-out", or "never". "Never" and "always" are informational, while "option" and "opt-out" means the default is on (optional) or off (opt-out) but can be changed.

## File locations

Describe where this file must be located for each platform. It needs to be predictable so that third-party applications or layers can locate them automatically.

### Linux - system package

If part of a packaged system installation where the binary executable goes into /usr/bin: Capability files should go into /usr/share/benchmarking/ and the name should be in the form

```
<binary name>.bench
```

eg if the binary you want to run is called 'vkcube', then its capability file should be '/usr/share/benchmarking/vkcube.bench'.

Reasoning: There is no precedent for putting non-executable files into /usr/bin on Linux.

### Linux - custom install

If the binary is not part of a system package installation, or its executable is not installed in /usr/bin, then its capability file shall have the same name as the executable with the extension '.bench' added, and it shall reside in the same path as the executable.

You **may** also add a symbolic link to it in $HOME/.local/share/benchmarking/ of the user doing the installation (if this directory does not exist, it should be created). The purpose of the symbolic link is to enable applications that want to discover benchmark-enabled apps on the system a way to do so. It makes more sense for applications with a more extensive support for this standard, supporting automation capabilities.

Example: If Hades is installed into /opt/hades with its executable in /opt/hades/bin/hades, then its capability file shall be /opt/hades/bin/hades.bench

Reasoning: While it may be tempting to attempt to replicate the logic of package system installs for other directories such as /opt and /usr/local, this would be hard without also mandating where apps can place their binaries, which is outside the scope of this standard. Therefore the only reliable location is next to the executable. This also neatly splits the responsibility for placing these files: Application developers and custom application installers (including 'make install') use this custom method, while package maintainers and packaging scripts use the system package method.

## Benchmark mode activation

Platform-specific manner in which the benchmarking mode is activated and a path to the JSON which tells the application how the user wants to use the benchmarking mode is given.

### Linux

To activate an enable file, set the environment variable BENCHMARKING_ENABLE_PATH to the full path of the enable file. Alternatively, you can set the environment variable BENCHMARKING_ENABLE_JSON to contain the JSON itself directly without using a file. This more direct approach can be useful for unit testing and more minimal use cases, but on some platforms the available space for environment values can be rather limited.

Setting both BENCHMARKING_ENABLE_PATH and BENCHMARKING_ENABLE_JSON should be considered an error, and the user should be warned and BENCHMARKING_ENABLE_JSON shall be ignored.

### Android

TBD For Android, we would require reading a adb setprop

## Enable file

The enable file is a JSON where the user describes how the benchmarking mode should be activated. It must only attempt to activate the features described in the application's capabilities file.

See the capabilities file description above for more information on possible values for fields that reference fields in the capabilities file.

Example:

```json
    {
        "target": "<name of app>",
        "scenes": [ "example" ],
        "results": "path/to/results/file",
        "intent": "showcase",
        "settings": {
                "example": "balanced"
            }
        },
        "capabilities": {
            "example": true
        },
        "workarounds": {
            "example": false
        }
    }
```

Description of the fields:

* "target": Must be the same string as the "name" field of the capabilities file. Use this to ensure that we are not acting on an enable signal meant for another application by accident. If the target does not match our name, no action described in the enable file shall be carried out, including not writing anything to the results file.
* "scenes": List of scenes to play
* "intent": Can be one of "showcase", "benchmark" and "testing". This may be used to set reasonable defaults for other values or application configurations.
* "results": Put any application specific results into this JSON file. It is described below. File should not exist on initiation of the run, and should be overwritten on success if it does
* "settings": As described above, settings that the application offers for modification and its new value.
* "capabilities": As described above, capabilities that the application offers and its new value.
* "workarounds": As described above, workarounds that the application allows the user to turn on or off and its new value.

## Results file JSON

Definition of a JSON format for application output, if any. Few mandatory fields, mostly just de-conflicting. The structure is based on existing results JSON files created by Kishonti benchmarks, and patrace, gfxreconstruct (?) and vktrace tools.

Example JSON:

```json
    {
        "workarounds": {
            "example": false
        },
        "app_version": "application version",
        "std_version": 1,
        "date": "ISO date string",
        "surface_width": 1280,
        "surface_height": 780,
        "enable_file": { ... },
        "platform_info": { ... },
        "run_info": { ... },
        "rendering_backend": "Vulkan",
        "results": [
            "fps": 100,
            "start_frame": 60,
            "end_frame": 6000,
            "start_time": 100023,
            "stop_time": 324000
            "scene": "my_scene_2000",
        ]
    }
```

Each field type:

| Field | Mandatory | Description |
| ----- | --------- | ----------- |
| workarounds | If 'workarounds' in capabilities | List of workarounds as defined in the capabilities file with their current value when the run was made |
| app_version | Yes | String describing the current application version |
| std_version | Yes | Float describing the current version of the standard supported by the application writing the results file |
| date | No | The current date in ISO format |
| surface_width | No ||
| surface_height | No ||
| enable_file | If enable file provided	| Verbatim copy of the provided enable file |
| platform_info | No | A free-form JSON dictionary where the application can put any information describing the current platform |
| run_info | No | A free-form JSON dictionary where the application can put any information describing the current run |
| results | On success | If the scene is looped, it may contain multiple entries. Otherwise it contains a single entry. See field descriptions below. Should not be present on error. |
| error	| On error | If the run failed, its value should be a free form string explaining the error. Should not be present on success. |
| init_time | No | The time in milliseconds since the system-defined Epoch that the application started initializing. |
| end_time | No | The time in milliseconds since Epoch that the application started terminating. |
| rendering_backend | No | If the application has multiple rendering backends, specify which one in this free form text field. |

Results fields:

| Field | Mandatory | Description |
| ----- | --------- | ----------- |
| frames | No | Frames generated while running the scene (if applicable) |
| time | Yes | Wall clock duration of the scene in milliseconds |
| scene | No | If there are different scenes, can specify the name of which one was run |
| start_marker | No | If the start point of the scene in the API command stream is marked, give name of the marker. For Vulkan, this would be VK_EXT_debug_utils. For GLES, this would be EXT_debug_marker. |
| stop_marker | No | As above, but for the stop point of the scene. |
| start_time | No | The time in milliseconds since Epoch that the scene started. |
| stop_time | No | As above, but when scene ended. If present, ```start_time``` must also be present, and (```stop_time``` - ```start_time```) must be equal to ```time```. |

## CMake integration

This is a way to do basic integration into a cmake setup:
* Add a directory ```benchmarking``` that contains .bench enable files.
* In CMakeLists.txt add a ```file(COPY ${PROJECT_SOURCE_DIR}/benchmarking/${ARGV0}.bench DESTINATION ${CMAKE_CURRENT_BINARY_DIR})``` for each relevant executable target where ${ARGV0} is the executable name
* As above, also add ```install(FILES ${PROJECT_SOURCE_DIR}/benchmarking/${ARGV0}.bench DESTINATION bin)``` in CMakeLists.txt for each executable target, assuming the target also installs into ```bin```

In order to add capability file symlinks during installation, also add:
```
set(SYMLINK_DIR "$ENV{HOME}/.local/share/benchmarking")
install(DIRECTORY DESTINATION "${SYMLINK_DIR}")
```
very early in the CMakeLists.txt and then for each executable target add:
```
install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_INSTALL_PREFIX}/bin/${ARGV0}.bench ${SYMLINK_DIR}/${ARGV0}.bench)")
```

If you want to enable automated testing with this standard into your CTest suite, you can do it in this way:

```
set(ENABLE_JSON "{\"target\": \"${ARGV0}\"}")
set_tests_properties(${ARGV0} ENVIRONMENT "BENCHMARKING_ENABLE_JSON=${ENABLE_JSON}")
```

## Examples

If you want to run an application in benchmarking mode on Linux, the simplest way is to use BENCHMARKING_ENABLE_JSON in this manner:

```
BENCHMARKING_ENABLE_JSON="{\"target\":\"vulkan_thread_1\"}" ./vulkan_thread_1
```

## Discussion

Q: Allow description of existing cmd line options, and map settings / capabilities entries to these?

Unfortunately there are too many different ways to specify and combine such options to be able to easily map them to our capabilities and settings.

Q: YAML instead of JSON?

Arguably looks much prettier, but every framework seems to have JSON support, while YAML is less common. Also YAML is more complex and may have security issues, where JSON is simpler by spec.

Q: Have app write out the capabilities file instead of bundling a JSON?

Having a separate capabilities file that needs to follow the executable is less than ideal. However, trying to provoke an export of the JSON from the app seems more difficult to do in a portable and reliable manner.

Q: The symbolic links to capabilities files will not work properly on multi-user systems.

A: Fixing this would require applications to install with root so that they can write to a world-readable location. That does not seem to be a good requirement to add. Having a world writable location for capabilities files on a multi-user system would be a security risk.

Q: Why delineate scenes in the command stream using debug markers instead of giving explicit frame numbers?

A: Once you start working with multiple windows or solutions like pbuffers or alternative presentation schemes, then frame counting is no longer clearly defined. Tools may also skip some frames during tracing, or count frame presentations not normally counted by the app.
