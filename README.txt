詰将棋を解くプログラム（OpenShogiLib利用）
※試しに書いたものであって、単に解くだけなら
OpenShogiLibのサンプルを使った方が圧倒的に速いです。

コンパイル・実行方法
# apt-get install libosl-dev
$ g++ -o tsume tsume.cc -losl
$ ./tsume 6 9 2 | ./GUIdisplay

