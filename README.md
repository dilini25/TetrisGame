# POP Raylib starter

Simple starter application for POP C assignment.

Please see materials on Blackboard for more details.

# Building

To build run the command:

```bash
/opt/pop/bin/build-wasm.sh src/main.c
```

This will generate a directory *out* with the WASM and index.html files for the 
Raylib program.

# Running

The very first time you run a POP WASM application you must run the command:

```bash
/opt/pop/bin/allocate_port.sh
```

You might need to start a new terminal instance for the update to take effect.
To check that everything is fine run the command:

```bash
echo $MY_PORT
```

This should output a 5 digit number.


To run the Raylib program in *out* simply run the command:

```bash
/opt/pop/bin/run-wasm.sh
```

This will run a web server that serves the *out* on the port you allocated above. This is forwarded from the 
remote server to your local machine, which means you can simply open the corresponding web page within a browser 
on your local machine using the address:

```bash
localhost:XXXXX
```

where *XXXXX* is the port number you allocated above.