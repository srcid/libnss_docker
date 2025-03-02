# DockerNSS

A NSS module that resolves docker containers' names and IDs that are running and on the default bridge network. It uses libcurl to talk to docker engine API and jansson to parse the answer.

## Warning

This is a personal project aimed for learning. It's not functional yet!

## Testing

First compile the code generate the `libnss_docker.so.2`.

```sh
make libnss_docker.so
```

Edit the file `/etc/nsswitch.conf`, put docker as an option in the line `hosts`. It will not break your system: NSS skips the modules it didn't find. Then you can run `getent hosts xxx.docker` with the `LD_LIBRARY_PATH`, so NSS can use it.

```sh
LD_LIBRARY_PATH=$PWD getent hosts pg.docker
```

## Debbuging

You can compile the module with debugger flag `-g`, link it with another c program, and use vscode debbuging tools, but I didn't find a way to simulate the `getent` behavior.
