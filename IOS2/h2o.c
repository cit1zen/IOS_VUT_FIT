#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <time.h> //Pre rand
#include <sys/wait.h>

//////////////////////////////MAKRA////////////////////////////////////


//Makro je volane ked nastane chyba
//vypisise chybovu hlasku
//a uprace po sebe procesi
//a main procesu povie nech sa ukonci exit(2)
#define FATAL_ERROR(n) perror(n); cleanup(); exit(2)

//Makro na zapis noveho pid_t do zoznamu
#define NEW_FORK(n) \
    if(n>0) \
    { \
        sem_wait(&premenne->sem); \
            zoznam_procesov[premenne->amount_proc]=n; \
            premenne->amount_proc++; \
        sem_post(&premenne->sem); \
    }


//////////////////////////////STRUKTURY/////////////////////////////////////


//Struktura na zdielane premenne
//a semafor na synchr. pristupu
typedef struct shared_variables{
    sem_t sem; //synch. pristupu
    unsigned amount_H; //pocet volnych H
    unsigned amount_O; //pocet volnych O
    unsigned bonding; //pre synchranizaciu bondingu
    unsigned finish; //pre synchronizaciu vypisu finished, aby sa vsetky programi pockali

    //Premenne pre error handling
    unsigned error;
    unsigned amount_proc; //pocitadlo kolko procesov sme uz vytvorili fork

    //Premenne na vypis
    unsigned index_akcie;
    FILE *subor;
}shared_variables;

//Semafory
//budu tiez v shared memory
//len som ich chcel mat v osve strukture
typedef struct semaphores{
    sem_t O_queue; //Semafor v ktorom cakaju kysliky kym budu moct byt pouzite
    sem_t H_queue; //Semafor v ktorom cakaju vodiky kym budu moct pouzite
    sem_t Bonding; //Semafor pre funkciu bond(), aby atomi ktore sa bonduju na seba pockali
    sem_t Next_Create_Bond; //Semafor na to aby sa netvorili nove molekuly alebo nezacal bond() uprostred bondovania molekuly
    sem_t Finished; //Semafor aby sa pockali kym su vsetky bond a az potom vypisali finished
    sem_t Error_handling;
}semaphores;


//////////////////////////////GLOBALNE PREMENNE/////////////////////////////////////


//Kedze su mnohonasobne pouzite, tak som z nich urobil globalne premenne
unsigned n; //Pocet atomov kyslika, vodik je 2xN
unsigned O_max_wait, H_max_wait, Bond_max_wait; //Max cakania na novy proces danej kategorie
//Zdielane premenne
shared_variables *premenne=NULL;
//Semafory
semaphores *semafory=NULL;
//Zoznam procesov keby sme ich nahodou potrebovali vycistit
//je vzdy pouzivani zo shared variables, takze je chraneni jej semaforom
//len ked nastane chyba tak nie je, ale to nevadi lebo sa pred vstupom zastavia vsetky procesi ktore by ho mohli modifikovat
pid_t *zoznam_procesov=NULL;


//////////////////////////////PROTOTYPY FUNKCII/////////////////////////////////////


//Nastavenie sturktury shared_variables
int shared_variables_init( shared_variables *premenne );

//Nastavenie semaforov v strukture semaphores
int semaphores_init( semaphores *semafory);

//Proces na vytvaranie procesov, prvkov
//n je pocet procesov ktore mame vytvorit
//typ je aky prvok to ma byt O = 0 H = 1
int generator(unsigned,unsigned);

//Proces prvku
int atom(unsigned ID,char prvok);

//Funckia pre nanosleep
//Vytvara nam cakanie
int go_to_sleep(long unsigned);

//Funckia na skladanie vody
void bond(char,unsigned);

//Funkcia zabezpecuju aby sa ukoncili vsetky procesi az ked su vsetky zviazane vo vode
void finished(char prvok,unsigned ID);

//Spracovanie prikazovej riadky
void argumenty(int argc, char* argv[]);

//Nastala chyba systemoveho volania, a je treba po sebe upratat
//aka zabit deti, resp. child procesi, a povedat main procesu aby skoncil z exit(2)
void cleanup();

//Funkcia na vypis do suboru
void vypis(const char *sprava,char prvok,unsigned ID);


//////////////////////////////MAIN/////////////////////////////////////


int main(int argc, char* argv[])
{
    //Spracovanie prikazovej riadky
    argumenty(argc, argv);

    //Zoznam procesov pre error handling
    zoznam_procesov =  mmap(NULL,sizeof( pid_t[ (3*n+2) ] ), PROT_READ | PROT_WRITE , MAP_SHARED | MAP_ANONYMOUS, -1 , 0);
    if( zoznam_procesov == NULL) //nepodaril sa mmap
    {
        FATAL_ERROR("zoznam_procesov_mmap");
    }

    semafory =  mmap(NULL,sizeof( struct semaphores ), PROT_READ | PROT_WRITE , MAP_SHARED | MAP_ANONYMOUS, -1 , 0);
    if( semafory == NULL) //nepodaril sa mmap
    {
        FATAL_ERROR("semafory_mmap");
    }
    //Nastavenie semafora a premennych na 0
    semaphores_init(semafory);

    //Vytvorime si zdielanu pamat
    //kedze pri fork sa prenasaju aj namapovane, tak to robim tu
    premenne =  mmap(NULL,sizeof( struct shared_variables ), PROT_READ | PROT_WRITE , MAP_SHARED | MAP_ANONYMOUS, -1 , 0);
    if( premenne == NULL) //nepodaril sa mmap
    {
        FATAL_ERROR("premenne_mmap");
    }
    //Nastavenie semafora a premennych na 0
    shared_variables_init(premenne);
    //Otvorime si subor na vypis
    premenne->subor=fopen("h2o.out","a");
    if(premenne->subor==NULL)
    {
        FATAL_ERROR("fopen");
    }



    //Generator kyslika
    pid_t O_gen=fork();
    //Zapiseme si pid procesu keby sme ho potrebovali v pripade chyby kill
    NEW_FORK(O_gen);
    if(O_gen >= 0) // fork sa podaril
    {
        if(O_gen == 0) // Toto robi O_gen
        {
            generator(n,0);
        }
        else // Toto Main
        {
            //Generator vodika
            pid_t H_gen=fork();
            //Zapiseme si pid procesu keby sme ho potrebovali v pripade chyby kill
            NEW_FORK(H_gen);
            if( H_gen >= 0 )
            {
                if( H_gen == 0 )//toto robi H_gen
                {
                    generator(2*n,1);
                }
            }
            else //fork zlyhal
            {
                fclose(premenne->subor);
                FATAL_ERROR("H_gen_fork");
            }
        }
    }
    else // fork zlyhal
    {
        fclose(premenne->subor);
        FATAL_ERROR("O_gen_fork");
    }

    //Caka kym generatory neskoncia
    //lebo generatory skoncia az ked skoncia vsetky procesi atomov
    while(wait(NULL)>0)
    ;

    //Uzavrieme subor kam sme zapisovali
    fclose(premenne->subor);

    //Ked funckia cleanup upratuje, tak zastane hlavny proces na semafore
    sem_wait(&semafory->Error_handling);

    //Ak nastala chyba a bola zavolana funkcia cleanup, tak error bude 1
    //v tejto fazy su tak ci tak vsetky ostatne procesi mrtve, takze nepouzivam semafor
    //lebo pouzitie semaforu by mohlo sposobit problemi ak bol zavolany cleanup
    //lebo nie je jasne v akom stave semafory zanechal
    if(premenne->error!=0)
    {
        exit(2);
    }
    else
    {
        exit(0);
    }
}


//////////////////////////////DEFINICIA FUNKCII/////////////////////////////////////


//Proces na vytvaranie procesov, prvkov
//n je pocet procesov ktore mame vytvorit
//typ je aky prvok to ma byt O = 0 H = 1
int generator(unsigned n,unsigned typ)
{
    //ID atomu
    unsigned atom_ID=1;

    //cyklus na vytvaranie novych prvkov
    pid_t novy_prvok = 1;
    while( atom_ID <= n && novy_prvok > 0)
    {
        //Vytvori proces z prvkom
        novy_prvok = fork();
        NEW_FORK(novy_prvok);
        if( novy_prvok < 0 ) //Ak by nastala chyba
        {
            FATAL_ERROR("GENERATOR");
        }
        if( novy_prvok == 0 )//toto robi vytvoreny proces
        {
            switch(typ)
            {
                case 0: //Ak to co mame vytvarat je kyslik
                {
                    atom(atom_ID,'O');
                    break;
                }
                case 1: //Ak je to vodik
                {
                    atom(atom_ID,'H');
                    break;
                }
            }
        }
        else if ( novy_prvok > 0) //Toto robi generator, simuluje to dlzky vytvarania prvkov
        {
            switch(typ)
            {
                case 0: //Ak generator vytvara kyslik
                {
                    go_to_sleep(O_max_wait); //Cakanie kym mozme vytvorit dalsi taky atom
                    break;
                }
                case 1: //Ak je to vodik
                {
                    go_to_sleep(H_max_wait); //Cakanie kym mozme vytvorit dalsi taky atom
                    break;
                }
            }
        }
        else //fork() nefungoval
        {
            FATAL_ERROR("GENERATOR");
        }
        //Pocitadlo a zaroven ID atomu
        atom_ID++;
    }

    //Caka az kym vsetky procesi atomov ktore vytvoril neskoncia
    while(wait(NULL)>0)
    ;

    exit(EXIT_SUCCESS);
}

//Funckia pre nanosleep
//Vytvara nam cakanie
int go_to_sleep(long unsigned max_wait)
{
    srand(time(NULL));
    //Nahodne cakanie
    max_wait=(rand() % (max_wait*1000000+1));
    const struct timespec nastavenia_spanku={ max_wait/1000000000000 , max_wait%1000000000 };
    //                                        aby tam mohlo byt 0.5 s   a toto su nanosekundy


    //Ak nastal problem z nanoslepp
    if(nanosleep(&nastavenia_spanku, NULL)!=0)
    {
        FATAL_ERROR("NANOSLEEP");
    }
    return 0;
}

//Funckia na skladanie vody
void bond(char prvok,unsigned ID)
{
    //Vypis hlasky begin bonding
    sem_wait(&premenne->sem);
    vypis("%u\t: %c %u\t:begin bonding\n",prvok,ID);
    sem_post(&premenne->sem);

    //Simulacia trvania skladania
    go_to_sleep(Bond_max_wait);

    //Samotne bondovanie
    sem_wait(&premenne->sem); //Pripoji sa na premenne
    premenne->bonding--;
    if(premenne->bonding==3)
    {
        vypis("%u\t: %c %u\t:bonded\n",prvok,ID);
        premenne->bonding--;
        sem_post(&semafory->Bonding);
        sem_post(&semafory->Bonding);
    }
    else
    {
        sem_post(&premenne->sem);
        sem_wait(&semafory->Bonding);
        sem_wait(&premenne->sem);
        vypis("%u\t: %c %u\t:bonded\n",prvok,ID);
        premenne->bonding--;
    }
    if(premenne->bonding==0)
    {
        sem_post(&semafory->Next_Create_Bond);
    }
    sem_post(&premenne->sem);
}

//Funkcia zabezpecuju aby sa ukoncili vsetky procesi az ked su vsetky zviazane vo vode
void finished(char prvok,unsigned ID)
{
    //Najprv je potreba zistit ci uz su zviazane vsetky procesi
    sem_wait(&premenne->sem);
    //Ak uz su zviazane vsetky procesi vody
    //3*n je procesov prvkov
    //-1 lebo ked sme zbondovali vsetky prvky tak sa ten posledny nepripocita ale spusti vypis finished
    if(premenne->finish!=(3*n-1))
    {
        premenne->finish++;
        sem_post(&premenne->sem);
        sem_wait(&semafory->Finished);

    }
    else
    {
        sem_post(&premenne->sem);
    }

    //Vypis hlasky finished
    sem_wait(&premenne->sem);
    vypis("%u\t: %c %u\t:finished\n",prvok,ID);
    sem_post(&premenne->sem);

    sem_post(&semafory->Finished); //Pusti dalsi proces na vypis
}

//Proces prvku
int atom(unsigned ID,char prvok)
{
    //Vypis hlasky started
    //moze sa vypisat prakticky kedykolvek, preto je pred semaforom
    sem_wait(&premenne->sem);
    vypis("%u\t: %c %u\t:started\n",prvok,ID);
    sem_post(&premenne->sem);

    //Semafor aby sme nenarusili proces bondovania
    sem_wait(&semafory->Next_Create_Bond);
    //Ziskame si pristup do zdielanej pamate
    sem_wait(&premenne->sem);

    //Zapiseme si do zdielanej premennej ze mame proces vhodny na bondovanie, danej kategorie
    if ( prvok == 'H' )
        premenne->amount_H++;
    else
        premenne->amount_O++;

    //Zistenie ci sa da viazat
    if( premenne->amount_H >= 2 && premenne->amount_O >= 1 ) //Ak je dost atomov na viazanie
        {
            vypis("%u\t: %c %u\t:ready\n",prvok,ID);
            premenne->bonding=6;
            sem_post(&semafory->H_queue);
            //Podla toho ci bond spustil vodik alebo kyslik sa prebudzaju procesi s ktorimi sa budeme bondovat
            if ( prvok == 'O' )
                sem_post(&semafory->H_queue);
            else
                sem_post(&semafory->O_queue);
            premenne->amount_H-=2;
            premenne->amount_O--;
            sem_post(&premenne->sem); //Koniec kritickej sekcie ak je dost na viazanie
        }
        else
        {
            vypis("%u\t: %c %u\t:waiting\n",prvok,ID);
            sem_post(&premenne->sem); //Koniec kritickej sekcie ak nie je dost na viazanie
            sem_post(&semafory->Next_Create_Bond);
            if ( prvok == 'O' )
                sem_wait(&semafory->O_queue);
            else
                sem_wait(&semafory->H_queue);
        }

    //Proces bondovania
    bond(prvok,ID);

    //Vypis hlasky finished
    finished(prvok,ID);

    exit(EXIT_SUCCESS);
}

//Spracovanie prikazovej riadky
void argumenty(int argc, char* argv[])
{
    //Su 4 argumenty pre program na prikazovej riadke
    if(argc==5)
    {
        n=(unsigned)atoi(argv[1]);
        H_max_wait=(unsigned)atoi(argv[2]);
        O_max_wait=(unsigned)atoi(argv[3]);
        Bond_max_wait=(unsigned)atoi(argv[4]);
        //Ak boli splnene vsetky podmienky zo zadania
        if( atoi(argv[1])>0 && O_max_wait<5001  && H_max_wait<5001  && Bond_max_wait<5001 )
        {
            return;
        }
    }
    //Ak bolo nieco zle podla zadania
    fprintf(stderr,"Chybne argumenty\n");
    exit(1);
}

//Nastala chyba systemoveho volania, a je treba po sebe upratat
//aka zabit deti, resp. child procesi, a povedat main procesu aby skoncil z exit(2)
void cleanup()
{
    sem_wait(&semafory->Error_handling);
    //Zastavim generatory
    //zastavujem preto aby sa neukoncil main preces predtym ako sa postaram o deti
    for(int index=0; index<2;index++)
    {
        //Musim osetrit pripad ze generator vyhodil chybu
        if(zoznam_procesov[index]!=0 && getpid()!=zoznam_procesov[index])
        {
            kill(zoznam_procesov[index], SIGSTOP);
        }
    }

    //Zapiseme do erroru aby sa main proces skoncil exit(2)
    //zapisujeme mimo semaforu, lebo toto je jedina funckia ktora z errorom nieco robi okrem mainu
    //a aj tak je toto rather nasilne ukoncenie
    premenne->error=1;

    //Cyklus ktori posle SIGKILL vsetkym procesom atomov a nakoniec aj generatorom
    //3*n procesi atomov
    //+2 procesi generatorov
    //-1 lebo pole zacina 0 takze 3*n+2 je mimo pola
    for(int index=(3*n+1);index>=0;index--)
    {
        //Overi si ci je v danej polozke pola zapisany dajaky pid_t
        //a je tu zaroven ochrana aby sme nevypli proces ktory vypina ostatne
        //printf("%d\t%d\n",index,zoznam_procesov[index]); DEBUG
        if(zoznam_procesov[index]!=0 && getpid()!=zoznam_procesov[index])
        {
            //Postupne to zrusi vsetky procesi
            kill(zoznam_procesov[index],SIGKILL);
        }
    }
    sem_post(&semafory->Error_handling);
}

//Nastavenie sturktury shared_variables
int shared_variables_init( shared_variables *premenne )
{
    premenne->amount_H=0;
    premenne->amount_O=0;
    premenne->bonding=0;
    premenne->error=0;
    premenne->amount_proc=0;
    premenne->index_akcie=0;
    //Inicializacia semafora
    if ( sem_init(&premenne->sem,1,1)!=0)
    {
            FATAL_ERROR("shared_var_sem_init:");
    }
    return EXIT_SUCCESS;
}

//Nastavenie semaforov v strukture semaphores
int semaphores_init( semaphores *semafory)
{
    if ( sem_init(&semafory->O_queue,1,0)!=0)
    {
            FATAL_ERROR("O_queue_init:");
    }
    if ( sem_init(&semafory->H_queue,1,0)!=0)
    {
            FATAL_ERROR("H_queue_init:");
    }
    if ( sem_init(&semafory->Bonding,1,0)!=0)
    {
            FATAL_ERROR("Bonding_init:");
    }
    if ( sem_init(&semafory->Next_Create_Bond,1,1)!=0)
    {
            FATAL_ERROR("Create_bonding_init:");
    }
    if ( sem_init(&semafory->Finished,1,0)!=0)
    {
            FATAL_ERROR("Finished_init:");
    }
    if ( sem_init(&semafory->Error_handling,1,1)!=0)
    {
            FATAL_ERROR("Error_handling_init:");
    }
    return EXIT_SUCCESS;
}

//Funkcia na vypis
void vypis(const char *sprava,char prvok,unsigned ID)
{
    //posunieme pocitadlo
    premenne->index_akcie++;
    //Posleme na vypis
    if( fprintf(premenne->subor,sprava,premenne->index_akcie,prvok,ID) < 0 )
    {
        FATAL_ERROR("FPRINTF");
    }
    //Vycistime buffer, zabezpecime aby sa to naozaj zapisalo do suboru
    //a nezostalo v buffery, a robilo kraviny
    if( fflush(premenne->subor) != 0 )
    {
        FATAL_ERROR("FFLUSH");
    }
}



