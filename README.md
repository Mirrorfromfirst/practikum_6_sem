Сборка
make
make CC=clang
make CC=gcc


Запуск примера
./bin/manager 2 127.0.0.1 5555 --a 0 --b 1 --n 1000000 --timeout 30
./bin/worker --host 127.0.0.1 --port 5555 --cores 2 --timeout 30
./bin/worker --host 127.0.0.1 --port 5555 --cores 2 --timeout 30

Проверки качества
make test       
make bench      
make analyze    
make docs       
make coverage   
make asan       
make ubsan      

