# system_properties
Magisk `resetprop` can set varies props including read-only properties(start with `ro.`), however it will also increase the serial number -- or to be exact, the lower bytes of the serial, let's call it counter -- and used by some apps for `resetprop` detection.

This `system_properties` utility works on the database file directly without increasing the serial. I forked the repo, adding some cli options to read/set the serial as well as primitive wildcard support.

### Usage

All changes will be gone after reboot, to make it "permeant", set it in a boot script as in Magisk module.

```
usage: system_properties [-h] [-c] [-l log_level] [-s] [-f] [-y] [-v] prop_name prop_value new_count*
  -h                   display this help message
  -c count             set count(optional)
  -l log_level:        console = 1(default) logcat = 2  consle + logcat = 3
  -s                   print security context(selabel)
  -f                   read property_contexts files to get security context
  -y                   auto confirm for new property
  -v                   verbose mode
```



- Read a property
  
  `system_properties ro.debuggable`

- Set a property value to 1, this will not change the counter.
  
  `system_properties ro.debuggable 1`

- Set a property value to 1 and counter to 0.
  
  `system_properties ro.debuggable 1 0`

- Keep old value and set count only  
  
  `system_properties -c 0 ro.debuggable`

- Add a new property with Y/N prompt. Use `-y` to supress the prompt.  
  
  `system_properties ro.new.prop 1`
  
- Read all `ro.` properties
  
  `system_properties ro.*`

- Set all `ro.` properties counter to 0 to cleanup trace from `resetprop`
  
  `system_properties -c 0 ro.*`



### Wildcard support

Supports only "begin with" "ends with" "includes" type of matching instead of regex.  

Examples:

  `ro.boot.*`  match all properties start with `ro.boot.`

  `*.version`  match those end with `version`

  `*driver*`   match those include `driver`

  `all` for all properties

  Wildcard match can be used along with `-c` option to set counters, but it doesn't support mass setting value for safety reasons.  

### Download

The pre-compiled binary is in `libs` folder.



**Please refer to original [README](https://github.com/lucidusdev/android_properties/blob/main/README.original.md) for more info.**















