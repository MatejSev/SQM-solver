# SQM Solver

Cílem projektu je paralelní algoritmus pro řešení problému pokrytí mřížky quatrominy. 

## Popis problému

Úlohou je nalézt pokrytí obdélníkové herní desky o rozměrech $a \times b$ s minimální cenou. 

* Pro rozměry desky platí $3 \le a$, $b \le 20$ a celková plocha $ab \ge 15$. 
* K pokrytí se využívají výhradně quatromina typu „T“ a „Z“, která lze při vkládání libovolně otáčet a překlápět. 
* Platí striktní omezení na paritu, kdy počet použitých dílků typu T a Z se musí rovnat, nebo se smí lišit maximálně o 1.
* Cena pokrytí je definována jako součet ohodnocení všech nepokrytých políček na desce.

## Struktura repozitáře

* **`MPI/`** – Implementace využívající hierarchickou architekturu Master-Slave kombinující zasílání zpráv (MPI) a sdílenou paměť (OpenMP na každém uzlu).
* **`OpenMP_data/`** – OpenMP implementace využívající datový paralelismus, která striktně odděluje fáze prohledávání do šířky (BFS) a paralelního prohledávání do hloubky (DFS).
* **`OpenMP_task_solver/`** – OpenMP implementace s využitím taskového paralelismu, která dynamicky rozděluje práci pomocí asynchronních úkolů přes direktivu `#pragma omp task`.
* **`sequential_solver/`** – Sekvenční algoritmus řešící problém prohledáváním stavového prostoru metodou větví a mezí (BB-DFS).
* **`maps/`** – Testovací datové sady obsahující základní mapy i vlastní generované náročné mapy (s příponou `hard.txt`).
* **`Results/`** – Složka obsahující naměřené výsledky a finální report dokumentující výkonnostní testy.

## Implementace a výsledky

Projekt řeší NP-těžký problém pokrývání mřížky pomocí prohledávání stavového prostoru s ořezáváním (Branch & Bound). Z provedených experimentů vyplývají následující klíčové závěry:

* Datový paralelismus prokázal výrazně vyšší efektivitu než taskový přístup díky eliminaci dynamické alokační režie.
* Taskový paralelismus s rostoucím počtem vláken škálovat přestává a u složitějších map naráží na zahlcení fronty úkolů.
* MPI implementace spolehlivě dekomponuje obrovské stavové stromy, avšak naráží na komunikační úzké hrdlo spojené se zpožděným šířením globálního minima po síti.
* U paralelního algoritmu bylo na reálných datech demonstrováno superlineární zrychlení způsobené dřívějším objevením silné ořezávací meze.
