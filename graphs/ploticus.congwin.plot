#proc getdata
file: congestion.plot
#proc areadef
title: Congestion Window
xautorange: datafield=1
yautorange: datafield=2
#proc xaxis
stubs: inc 10000
minorticinc: 2500
#proc yaxis
stubs: inc 4
minorticinc: 1
#proc lineplot
xfield: 1
yfield: 2
linedetails: color=red width=0.5

