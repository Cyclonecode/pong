# pong
Very simple socket server

## Running

The server needs to be started using sudo privileges and you also need
to supply a port i.e:

    sudo ./pong 3000

You should then be able to connect and get a quote:

    curl 127.0.0.1:3000

Notice that the program assumes that you have created a `banner` file and `quotes.txt`, the contents of these files is not really important.
