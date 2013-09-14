#proc getdata
file: avg_bw.plot
#proc areadef
title: avg bw
xautorange: datafield=1
yautorange: datafield=2
#proc xaxis
stubs: inc 10000
minorticinc: 500 
#proc yaxis
stubs: inc 1000
minorticinc: 250
#proc lineplot
xfield: 1
yfield: 2
linedetails: color=red width=0.5

