# Tool - Java annex-b nalu buffer processor

Logic to replace h264 nalu identifier with the nalu size.

# HowTo:

Check the Exec.java source code to get some clue what's happening there.

```
$ cd tools/java_modify_videobuffer

$ javac --version
javac 11.0.6

$ javac Exec.java

$ java Exec
Test 24851
Found byte array: 606
Start: 0, end: 24851
```

Now you can compare frame.raw and outframe.raw and check the binary difference
