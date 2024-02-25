# Client-server message management app

## _Structura de functionare a serverului:_
---

Serverul foloseste multiplexare cu epoll pentru a realiza gestiunea mesajelor
de la, respectiv catre clienti.
Adaugam file descriptori de socketi in lista de interese a epoll si ne asiguram
ca de fiecare data cand unul dintre acesti file descriptori este pregatit pentru
a efectua o operatie, stim ce operatie trebuie sa efectueze.

Structura 'Client' reprezinta un client TCP care s-a conectat la un moment dat la
serverul nostru. Printre altele, aceasta retine informatii precum:
- ID-ul clientului
- IP si port
- sockfd
- starea conexiunii

La nivelul serverului exista o baza de date (lista simplu inlantuita) ce retine
toate structurile clientilor conectati cu succes la un moment dat la server, fie
ei conectati sau deconectati la momentul actual.

### Procesul de conectare a unui client TCP la server:
---

Cand se apeleaza functia 'connect' pe socket-ul TCP al serverului, rulam un
handler aferent unei noi conexiuni, in cadrul caruia cream socket-ul si structura
aferenta clientului. Adaugam aceasta structura la baza de date, cu starea
conexiuni setata ca 'STATE_ID_NEEDED'.

Al doilea pas este trimiterea ID-ului de la client la server sub forma de
mesaj, de data aceasta folosind socket-ul aferent clientului. La aparitia
evenimentului in epoll, cautam in baza de date structura clientului cu sockfd
egal cu cel specificat de eveniment si trecem la primirea datelor.

Dupa ce primim intregul mesaj (posibil in mai multi pasi), parsam continutul si
verificam daca ID-ul specificat este asociat deja unui alt client. In caz
afirmativ, daca starea conexiunii acestuia este 'STATE_CONNECTED', afisam
mesajul "Client <ID> already connected." conform enuntului si eliberam
resursele alocate noului client. Tot in caz afirmativ, dar daca starea
clientului este 'STATE_DISCONNECTED', avem de a face cu un client care se
reconecteaza, asa ca ii actualizam datele (sockfd, IP, port, stare) si
eliberam resursele alocate pentru acelasi client inainte de primirea
ID-ului.
In cazul in care nu gasim un client cu acelasi ID, avem de a face cu un nou
client, caz in care completam ID-ul acestuia in structura si continuam.

Pentru o functionare mai rapida a operatiilor de cautare de clienti, avand
in vedere ca ID-urile acestora sunt unice, folosim o structura de treap,
cu prioritati setate aleator pentru o forma echilibrata, ce are drept chei
in noduri ID-urile clientilor. Adaugarea la acest treap se realizeaza dupa 
ce am primit ID-ul de la un client nou.

### Comunicarea intre server si clientii TCP:
---

Pentru a putea delimita corect mesajele trimise intre clienti si server,
folosim mai multe caractere speciale (character stuffing) pentru a ne
asigura ca se identifica in mod corect inceputul si finalul unui mesaj.
Mai exact, folosim caracterele 'DLE', 'STX' si 'ETX', cu urmatoarea
semnificatie:
- DLE STX -> inceput de mesaj
- DLE ETX -> sfarsit de mesaj
- DLE DLE -> caracterul DLE

Fiecare structura de client contine un buffer de primire. Daca se trimit
bytes de la acel client, la fiecare apel 'recv' parcurgem continutul
byte cu byte si il interpretam. Adaugam la bufferul clientului tot
continutul ce se afla intre doi delimitatori de mesaj, cu posibilitatea
realizarii acestei operatii in mai multi pasi. Pentru a putea avea
aceasta posibilitate de completare ulterioara a unui mesaj, retinem
intr-o variabila din structura clientului daca am intalnit ca ultim
caracter in bufferul primit curent caracterul special 'DLE', pentru a-i putea
interpreta la urmatorul apel 'recv' semnificatia.
In momentul in care ajungem la secventa "DLE ETX" si stim ca mesajul s-a
incheiat, apelam o functie de interpretare a continutului acestuia, iar apoi
reinitializam bufferul de primire.

Pentru trimiterea de mesaje de la server la client folosim o coada de structuri
de tip 'Task', ce contin o referinta catre un mesaj primit de la clientul UDP
(intr-o forma comprimata si prelucrata pentru ca mesajul sa poata fi delimitat,
folosind aceleasi caractere speciale mentionate mai sus), impreuna cu un contor
pentru numarul de bytes trimisi pana la momentul curent din acel mesaj (pentru a
sti cand am finalizat task-ul). Trimiterea mesajului se realizeaza apoi prin
eventual multiple apeluri 'send'.

### Tipurile de mesaje transmise intre server si clientii TCP:
---

Pentru fiecare tip de mesaj am creat un protocol respectat atat de clientii TCP,
cat si de server. Acestea se bazeaza pe gruparea datelor intr-un spatiu cat mai
mic, pentru a folosi eficient reteaua si a obtine timpi mai scurti de transmisie.
Spre exemplu, la transmiterea datelor precum ID-ul clientului, numele unui topic
sau continutul unui mesaj de la clientul UDP, unde lungimea poate varia, am
atasat lungimea acestora in mesaj pentru a putea trimite strict numarul de octeti
necesar, pentru a nu folosi lungimi predefinite care sa presupuna octeti de
umplere inutili.
Astfel, consider ca protocoalele implementate pentru reprezentarea mesajelor sunt
eficiente, atat ca spatiu cat si ca timp efectiv de transmisie.

De la client catre server avem urmatoarele tipuri de mesaje:

- subscribe la un topic, cu un anumit SF

Pentru acest tip de mesaj se foloseste urmatorul format (protocol de nivel
aplicatie):

<pre>
operation_type: 1 byte
total_size: 2 bytes
sf: 1 byte
topic: toate caracterele pana la intalnirea \0 (ce coincide si cu
finalul mesajului - lungimea sa se deduce din diferenta intre lungimea
totala a mesajului si lungimea celorlalte doua campuri = 3 bytes)

|  1 byte |   2 bytes  |  1 byte  |                   |
| op_type | total_size |    sf    |      topic\0      |
</pre>

- unsubscribe topic

Pentru acest tip de mesaj se foloseste urmatorul protocol:

<pre>
operation_type: 1 byte
total_size: 2 bytes
topic: toate caracterele pana la intalnirea \0, analog

|  1 byte |   2 bytes  |                   |
| op_type | total_size |      topic\0      |
</pre>

- pasarea ID-ului de la client la server

Pentru acest tip de mesaj se foloseste urmatorul protocol:

<pre>
operation_type: 1 byte
total_size: 2 bytes
id: toate caracterele pana la intalnirea \0, analog

| 1 byte  |   2 bytes  |                |
| op_type | total_size |      id\0      |
</pre>

De la server catre client avem un singur tip de mesaj, cel primit de la
clientul UDP, in forma prelucrata:

Pentru acest tip de mesaj se foloseste urmatorul protocol:

<pre>
ip : 4 bytes
port : 2 bytes
data_type : 1 byte
content_size : 2 bytes
content : urmatorii content_size bytes
topic_size : 2 bytes
topic : urmatorii topic_size bytes

| 4 bytes |  2 bytes |  1 byte   |  2 bytes   |  
|   ip    |   port   | data_type | content_sz |

| content_sz bytes |  2 bytes |   topic_sz bytes  |
|     content      | topic_sz |       topic       |
</pre>

### Comportament functii de subscribe si unsubscribe:
---

_Functia de subscribe:_

Parseaza continutul mesajului cu formatul specificat mai sus, copiaza topicul
si elimina abonamentul aceluias client la acelasi topic, daca acesta exista,
urmand sa adauge un nou abonament cu atributele date.

Serverul retine o baza de date de topic-uri, fiecare topic avand asociata o lista
de abonamente (structuri ce contin referinta la structura clientului abonat si
argumentul SF asociat topicului). Aceasta baza de date este implementata sub forma
unui treap, asemanator celui folosit pentru clienti, dar cheile fiind numele
topicurilor. In acest fel putem gasi mai rapid un topic.

Abonarea se face adaugand o intrare in lista de abonamente asociata nodului
(topicului).

_Functia de unsubscribe:_

Se comporta in mod analog functiei de subscribe, dar elimina o intrare din lista
asociata topicului din treap.

### Primirea de mesaje de la clienti UDP:
---

La primirea unei datagrame, parsam pentru inceput continutul pentru a identifica
topicul acesteia. Avand topicul, il cautam in treap-ul de topicuri. Daca nu l-am
gasit inseamna ca niciun client nu e abonat la acesta, deci aruncam datagrama.

Altfel, pregatim datagrama pentru a fi trimisa catre eventualii clienti TCP
care o pot primi. O reorganizam si adaugam datele necesare pentru a respecta
protocolul descris mai sus, iar apoi parcurgem mesajul rezultat si adaugam
caracterele speciale, de delimitare. Bufferul rezultat va face parte dintr-O
structura de tip 'Datagram', care va mai contine si lungimea acestuia, precum
si un contor de referinte la aceasta, pentru a sti cand putem sa stergem
mesajul din memorie. 

Parcurgem apoi lista de abonati a topicului iar pentru clientii conectati
(indiferent de argumentul SF), sau pentru cei deconectati dar cu SF = 1, se
creaza cate o structura de tip 'Task' cu referinta catre structura 'Datagram'
pregatita pentru trimitere. Pentru clientii conectati, se activeaza EPOLLOUT
pentru a incepe trimiterea, iar pentru cei deconectati se va activa EPOLLOUT la
reconectare, daca exista task-uri in coada proprie clientului.

La finalizarea unui task, se decrementeaza contorul de referinte la structura
datagramei de trimis, iar daca acesta devine nul se elibereaza resursele
alocate (se sterge mesajul din memorie).

### Comenzile primite de la tastatura:
---

La primirea comenzii 'exit' de la tastatura se inchid socketii serverului si cei
ai clientilor, se elibereaza toate resursele alocate bazelor de date de clienti
si de topicuri si se inchide serverul.


## _Structura de functionare a clientului TCP:_
---

Clientul foloseste, precum serverul, multiplexare cu epoll pentru a gestiona
mesajele primite de la server si de la tastatura, precum si pentru a trimite
mesaje catre server.

### Prelucrarea comenzilor de la tastatura
---

Se parseaza linia citita si se identifica una dintre comenzile de subscribe,
unsubscribe sau exit. In cazul in care comanda nu coincide cu niciunul dintre
aceste tipuri, nu se efectueaza nicio operatie. Altfel, in functie de tipul
comenzii verificam numarul de argumente primit pentru a coincide cu cel
asteptat.

Pentru comanda de subscribe formam un mesaj dupa acelasi protocol descris in
sectiunea serverului, delimitam mesajul dupa acelasi procedeu ca cel de la
server si cream un task de trimitere a acestui mesaj catre server, pe care
il adaugam in coada de task-uri. In cazul clientului, exista o singura coada
de task-uri la nivelul aplicatiei.

Pentru comanda de unsubscribe procedam analog.

Pentru comanda de exit, inchidem socketul clientului si terminam programul.

### Primirea de mesaje de la server:
---

Vom primi doar mesaje provenite de la clientii UDP. Parcurgem mesajul dupa
acelasi principiu ca cel folosit la primirea de mesaje de la clientii TCP
in server, pentru a putea primi mai multe mesaje la un singur 'recv' / un
mesaj la mai multe apeluri 'recv'. Dupa ce avem un mesaj complet in bufferul
de primire al clientului, ii prelucram continutul (in cazul datelor numerice)
si il afisam in intregime.


## _Alte mentiuni:_
---

Atat serverul, cat si clientul, contin un mod de debug ce afiseaza mai multe
mesaje in plus, informative / de eroare, pe parcursul executiei. Acesta se
activeaza adaugand argumentul "--debug" la pornire, dupa argumentele necesare.

La inceputul ambelor programe am dezactivat bufferingul la afisare, iar pentru
toti socketii TCP am dezactivat algoritmul Nagle (TCP_NODELAY).

De asemenea, pentru structurile de lista simplu inlantuita, respectiv treap,
am folosit implementarile de la laboratoarele de Structuri de Date si Algoritmi
de anul trecut. 

Pentru delimitarea mesajelor la transmisia intre clientul TCP si server am
folosit o idee prezentata in laboratorul 2 de PCom (Data link).

Pentru partea de multiplexare, am plecat de la implementarea temei 3 de la
Sisteme de Operare, de semestrul trecut. Tot de acolo am preluat si headerul
'w_epoll.h' ce contine wrappere utile peste functiile specifice epoll.


## _Bibliografie:_
---

https://pcom.pages.upb.ro/labs/
https://man7.org/linux/man-pages/man2/socket.2.html
https://man7.org/linux/man-pages/man2/send.2.html
https://man7.org/linux/man-pages/man2/recv.2.html
https://man7.org/linux/man-pages/man2/bind.2.html
https://man7.org/linux/man-pages/man2/listen.2.html
https://man7.org/linux/man-pages/man2/connect.2.html
https://man7.org/linux/man-pages/man2/accept.2.html
https://man7.org/linux/man-pages/man7/tcp.7.html
https://man7.org/linux/man-pages/man7/udp.7.html
https://man7.org/linux/man-pages/man7/ip.7.html
https://man7.org/linux/man-pages/man7/epoll.7.html
