
echo -ne "    GET /  HTTP/1.1\nHost: test.de\n\n" | nc localhost 9001

echo -ne "\nGET /  HTTP/1.1\nHost: test.de\n\n" | nc localhost 9001

echo -ne "\rGET /  HTTP/1.1\nHost: test.de\n\n" | nc localhost 9001

echo -ne " \rGET /  HTTP/1.1\nHost: test.de\n\n" | nc localhost 9001

echo -ne " \nGET /  HTTP/1.1\nHost: test.de\n\n" | nc localhost 9001

echo -ne "\r\rGET /  HTTP/1.1\nHost: test.de\n\n" | nc localhost 9001

echo -ne "\r\tGET /  HTTP/1.1\nHost: test.de\n\n" | nc localhost 9001

echo -ne "GET http://hello.de/  HTTP/1.1\n\n" | nc localhost 9001

echo -ne "GET http://hello.de/  HTTP/1.1\nHost: test.de\n\n" | nc localhost 9001

echo -ne "GET ../  HTTP/1.1\nHost: test.de\n\n" | nc localhost 9001

echo -ne "GET /test.de  HTTP/1.1\nHost: localhost\n\n" | nc localhost 9001

GET /index.html HTTP/1.1
POST /index.html HTTP/1.1
DELETE /index.html HTTP/1.1

# Content-Type: text/plain
# Host: localhost:900
# Content-Length: 12


make run
(while true; do nc -l 900 ; done) | nc localhost 80
echo -ne '' | nc localhost 900



echo -ne '-' | nc localhost 900
echo -ne 'GET / HTTP/1.1\nHost: local\n\n' | nc localhost 900

echo -ne '_' | nc localhost 900
echo -ne 'GET / HTTP/1.1\nHost: local\n\n' | nc localhost 900

echo -ne 'GET\0 / HTTP/1.1\nHost: local\n\n' | nc localhost 900


URI=$'http://example.com/:@-._~!$&\'()*+,=;:@-._~!$&\'()*+,=:@-._~!$&\'()*+,==?/?:@-._~!$\'()*+,;=/?:@-._~!$\'()*+,;==#/?:@-._~!$&\'()*+,;='
URI=$'/:@-._~!$&\'()*+,=;:@-._~!$&\'()*+,=:@-._~!$&\'()*+,==?/?:@-._~!$\'()*+,;=/?:@-._~!$\'()*+,;==#/?:@-._~!$&\'()*+,;='
URI=$'/:@-._~!$&\'()*+,=?/?:@-._~!$\'()*+,;=/?:@-._~!$\'()*+,;==#/?:@-._~!$&\'()*+,;='
printf "\r\nGET %s HTTP/1.1\n" "$URI" | nc localhost 80


```
GET / HTTP/1.1
Host: localhost:900
Connection::: ::: close
abc:: test
abc: test
```

```
GET /1/../ HTTP/1.1
Host: localhost:900
Connection: keep-alive
Content-Length: 1

aGET /1/ HTTP/1.1
Host: localhost:900
Connection: keep-alive
Content-Length: 0
```


```
GET / HTTP/1.1
Host: d
Connection: hallokeep-alivehalloclosehallokeep-alivehallo
Content-Length: 0
```


// get out of root dir with .. % encoding



echo -ne 'GET / HTTP/1.1\nHost: local\n\nPOST / HTTP/1.1\nHost: local\nContent-Length: 4\n\n1234GET / HTTP/1.1\nHost: local\n\n' | nc localhost 900


