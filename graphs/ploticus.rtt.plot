#proc getdata
file: rtt.plot
#proc areadef
title: RTT
xrange: 0 2450
yrange: 180 750
#proc xaxis
stubs: inc 500
minorticinc: 100
#proc yaxis
stubs: inc 250
minorticinc: 45
#proc lineplot
yfield: 2
linedetails: color=red width=0.5

