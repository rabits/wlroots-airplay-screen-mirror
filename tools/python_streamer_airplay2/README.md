# Tool - airplay 2.0 python implementation

Partially implemented airplay 2.0 protocol without fp-setup (fairplay) due to complexety of the
client logic

# HowTo

Check the script logic. You need to create venv and run the script (it will fail).
```
$ cd tools/python_streamer_airplay2

$ python3 -m venv .venv

$ source .venv/bin/activate

[venv]$ pip install -r requirements-airplay2.txt

[venv]$ ./airplay2-screencast.py 192.168.30.243 7000
...
```
