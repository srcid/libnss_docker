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

## ToDo

- [ ] split the project, now it's only one C file.
- [ ] implement units tests, now I'm testing manually

## Related Project

This project main aim was learning, you may want to look at those other projects.

- [nss-docker](https://github.com/dex4er/nss-docker)
- [docker-nss](https://github.com/danni/docker-nss)
- [nss-docker](https://github.com/costela/nss-docker)
- [nss-docker-ng](https://github.com/petski/nss-docker-ng)
