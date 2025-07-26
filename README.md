This is a simple UDP heartbeat monitor thing.

The power at my house seems to go out entirely too frequently (I don't think
Dominion Energy manages to get even three 9's of reliability at my house).

Sometimes, I'm at the hackerspace at night, and I think, "I guess I'll go home
and get something to eat." only to arrive home to find the power is out, and
then I wished I'd just stayed at the hackerspace a few more hours until bed time,
because it's boring just sitting there in the dark.

So I came up with this pair of dumb programs, one of which periodically sends a heartbeat,
and one which receives and logs these heartbeats.  I run the 2nd program in the cloud,
then I can interrogate it and find out when the last heartbeat was received.  If it was
recent, then I know I probably have power at home, because otherwise the Raspberry Pi
that sends the heartbeat would not be powered on.

So now I can check before I leave the hackerspace whether there's power at my house.

How pitiful is Dominion Energy that I felt the need to create this dumb system?
Pretty damn pitiful.

