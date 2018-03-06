This folder contains an example implementation of a HAL client for ant.

This example expects to be able to use a unix domain socket located at
/dev/socket/ant. The sepolicy directory shows example policy to use
as a starting point for the vendor policy, and vintf contains a fragment
that should be added to the vendor manifest.xml in order to advertise
that the service is provided.

Note that the example policy does not currently contain the pieces that
are needed to actually open and use the socket.

