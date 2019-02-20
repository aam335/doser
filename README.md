## doser - 
benchmark tool for parallel http server tests
depends on libev library
### Installation 
```
$ sudo apt-get install libev-dev

download && make
```
### Examples
```
$ ./doser -v http://domain.com:8099/0/domain -c4000 -l8000 
0.0000 # http benchmarking tool
5.1074 # Done in 5.100916 sec, 1541.291683 ips, 7873.879312 bps (body)
5.1074 # internal (connect) errors: 138
5.1074 # external (http server tcp reject) errors: 1168
5.1074 code	min	max	avg	total queries
5.1074 200	0.0019	3.4771	0.7850	6694
5.1074 500	0.0293	0.0293	0.0293	1
5.1074 >>Connection timed out	5.0005	5.0005	5.0005	1167
```
```
$ ./doser -v http://domain.com:8099/0/domain -c4000 -l8000 -w10
0.0000 # http benchmarking tool
10.1009 # Done in 10.100645 sec, 786.682430 ips, 4458.725132 bps (body)
10.1009 # internal (connect) errors: 54
10.1009 # external (http server tcp reject) errors: 440
10.1009 code	min	max	avg	total queries
10.1009 200	0.0058	8.0506	1.2340	7506
10.1010 >>Connection reset by peer	6.7024	6.7056	6.7045	432
10.1010 >>Connection timed out	10.0004	10.0004	10.0004	8
```
```
$ ./doser -v http://domain.com:80/0/domain -c4000 -l8000 -w10
0.0000 # http benchmarking tool
10.1575 # Done in 10.150573 sec, 774.537518 ips, 3974.553756 bps (body)
10.1575 # internal (connect) errors: 138
10.1575 # external (http server tcp reject) errors: 1138
10.1575 code	min	max	avg	total queries
10.1575 200	0.0022	8.8739	1.9746	6724
10.1576 >>Connection timed out	10.0502	10.0502	10.0502	1138
```
```
$ ./doser 
0.0000 # http benchmarking tool
0.0002 Usage: ./doser uri [-csimcount] [-llimitcount] [-ttimelimit] [-wconntimeout]
0.0002 -v[v...] - verbose
0.0002 -cNumber - number of parallel queries
0.0002 -lNumber - limit queries count (default 1000)
0.0002 -tNumber - limit total test time in ms (default 5s)
0.0002 -wNumber - connection timeout (default 5s)
```