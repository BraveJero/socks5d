# socks5d

Implementacion de "socks5d" un proxy SOCKS version 5 en esteroides.

Desarrollado por:

- [García Matwieiszyn, Juan I](https://github.com/juani-garcia)
- [Colonnello, Joaquin](https://github.com/JColonnello)
- [Bartellini Huapalla, Mateo F](https://github.com/mbartellini)
- [Brave, Jerónimo](https://github.com/BraveJero)

## Objetivo

Diseñar e implementar un servidor proxy para el protocolo SOCKSv5 [[RFC1928]](https://datatracker.ietf.org/doc/html/rfc1928) que soporte una serie de requerimientos dictados por la catedra de [72.07] - Protocolos de Comunicación @[ITBA](https://www.itba.edu.ar/) y una aplicaccion cliente.

## Compilacion & requisitos

### Requisitos
- [GCC](https://www.geeksforgeeks.org/how-to-install-gcc-compiler-on-linux/) (>=9.4.0)
- [Make](https://linuxhint.com/install-make-ubuntu/) (>=4.2.1)

### Compilacion

```socks5d $ make```

###  Artefactos generados

Esto generará dos carpetas nuevas build/ y client/build que contienen los archivos socks5d y client respectivamente.

> Se puede re-construir con `make rebuild` o limpiar el proyecto con `make clean`

## Ejecucion

### Servidor

Para ejecutar el servidor se debe correr el comando:

```socks5d $ ./build/socks5d [OPTIONS] ... -t [TOKEN]```

Donde `-t [TOKEN]` no es obligatorio, de no incluir un token no se levantan los puertos de management. 
`[OPTIONS]` puede contener los flags especificados en el manual del protocolo, este se puede acceder con `man ./socks5d.8`

### Cliente

Para ejecutar el cliente se debe correr el comando:

```socks5d $ ./client/build/client [OPTIONS] ... [TOKEN]```

Donde `[TOKEN]` debe ser el mismo que se especifico antes. 
`[OPTIONS]` puede ser consultado ejecutando `./client/build/client -h`

## Adicionales

`informe.pdf` contiene las decisiones de diseño, una descripción de los protocolos y aplicaciones desarrolladas, los problemas encontrados, las limitaciones de la aplicacion y mas.

