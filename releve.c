/*
 * Micro-projet 2 : Simulation relevé periodique de température en temps réel
 */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <semaphore.h>
#include <stdint.h>
#include <signal.h>

//periode en milliseconde du relevé (1/2 seconde)
#define PERIODE_MSEC 500000

//structure d'un relevé
struct releve {
	char dateTime[sizeof "JJ/MM/AAAA HH:MM:SS"]; //format date
	int temperature;
} releve;

//Structure stockant l'heure
time_t now;
//Structure stockant la date
struct tm tm_now;

//thread et attributs
pthread_t tacheReleve,tacheR1;
pthread_attr_t tacheReleve_attr,tacheR1_attr;
//semaphore de synchronisation entre Releve et Ecriture
sem_t semR1;
//mutex pour protection de la structure releve
pthread_mutex_t mutexReleve;
struct releve rel;
//pour l'ecriture dans un fichier (voir code Ecriture)
FILE *f;
//descritpeur timer
int fdTimer;
//pour arreter l'execution
int flagFin = 0;
//Fonction appellée si l'utilisateur presse CTRL+C (voir code Main)
void stop(){
	flagFin = 1;
}

//fonction permettant la création et le parametrage d'un thread temp réel
int createPosixTask(char * nomTache, int priorite, int taillePile,int periode, pthread_t *tache, pthread_attr_t *tache_attr, void*(*codeTache)(void*)){ 
	//paramtrage des attributs du thread
	pthread_attr_init(tache_attr); //initialisation attributs
	pthread_attr_setstacksize(tache_attr,taillePile); // initialisation de la pile
	pthread_attr_setschedpolicy(tache_attr,SCHED_FIFO); //politique d'ordonnancement (ici FIFO)
	struct sched_param tache_param = {.sched_priority = priorite}; // definition de la priorité de la tache
	pthread_attr_setschedparam(tache_attr, &tache_param); //attribution de la propriété
	pthread_attr_setdetachstate(tache_attr,PTHREAD_CREATE_JOINABLE); 
	//pthread_setname_np(*tache,nomTache); // l'attribution d'un nom à la tache provoque des erreurs d'execution
	if(periode !=0){
		//Recuperation du temps courant
		struct timespec currentTime;
		if(clock_gettime(CLOCK_MONOTONIC,&currentTime)){
			printf("Erreur lors de la recuperation du temps courant! \n");
			exit(1);
		}

		//creation du timer
		if((fdTimer = timerfd_create(CLOCK_MONOTONIC ,0)) == -1){
			printf("Erreur lors de la creation du timer ! \n ");
			exit(1);
		}

		//parametrage et demmarage du timer
		struct itimerspec conftimer;
		if(periode >= 1000000){
			conftimer.it_interval.tv_sec = periode/1000000;
			conftimer.it_interval.tv_nsec = periode%1000000;
		}else{
			conftimer.it_interval.tv_sec = 0;
			conftimer.it_interval.tv_nsec = periode*1000;
		}
		currentTime.tv_sec += 1; // demarrage au temps courant + 1	
		conftimer.it_value = currentTime;
		if(timerfd_settime(fdTimer,TFD_TIMER_ABSTIME,&conftimer,NULL)){
			printf("Erreur impossible de demarrer le timer! \n");
			exit(1);
		}
	}
	//activation du thread
	if((pthread_create(tache,tache_attr,(void*(*)(void*))codeTache,(void*)NULL)!=0)){
		printf("Erreur de creation du thread ! \n");
		exit(1);
	}

}// fin de createPosixTask

//fonction waitPeriod()
void waitPeriod(){
	uint64_t topstimer;
	if(read(fdTimer,&topstimer,sizeof(topstimer))<0){
		printf("Attend sur timer echoue ! \n");
	}else{
		if(topstimer > 1){ // la tache a rate des activations
			printf("periode marquee pour le thread nb : %lu \n", (long unsigned int)topstimer);
		}
	}
}

//code de la releve de temperature
void* codeReleve(void* arg){
	int cpt = 1;
	while(!flagFin){
		waitPeriod(); // attente 1 sec
		time_t date_actuelle;
		//lecture heure courante
		now = time (NULL);
		//conversion en heure locale
		tm_now = *localtime (&now);
		//On protege la variable contenant le relevé (sémaphore de blockage mutuel)
		pthread_mutex_lock(&mutexReleve);
		strftime (releve.dateTime, sizeof releve.dateTime, "%d/%m/%Y %H:%M:%S", &tm_now);
		rel.temperature = rand()%40;
		//on debloque l'acces au relevé
		pthread_mutex_unlock(&mutexReleve);
		//on autorise l'execution de codeEcriture
		sem_post(&semR1);
		}
	return 0;
}

//code affichage console et ecriture dans fichier
void * codeEcriture(void * arg){
	while(!flagFin){
		//on attend la disponibilité de la semaphore de synchronisation (délivrée par cordeReleve)
		sem_wait(&semR1);
		//On protege la variable contenant le relevé
		pthread_mutex_lock(&mutexReleve);
		//ecriture dans le fichier
		fprintf(f, " %s | temperature: %d °C\n", releve.dateTime, rel.temperature);
		//affichage en console
		printf(" %s | temperature: %d °C\n", releve.dateTime, rel.temperature);
		//on debloque l'acces au relevé
		pthread_mutex_unlock(&mutexReleve);
	}
	return 0;
}

int main(){
	//on ouvre le fichier
	f = fopen("resultat.txt", "w");
	if (f == NULL){
		printf("Erreur dans l'ouverture du fichier\n");
		exit(1);
	}
	//vérouillage des pages mémoires
	mlockall(MCL_CURRENT | MCL_FUTURE);
	if((sem_init(&semR1,0,0)) == -1){
		printf("impossible de creer sem1 \n");
		exit(1);
	}
	//initialisation mutex
	if((pthread_mutex_init(&mutexReleve,NULL)!=0)){
		printf("Erreur lors de l'initialisation du mutex \n");
		exit(1);
	}
	//initialisation threadR1 (codeEcriture)
	if((createPosixTask("Ecriture",2,200,0,&tacheR1,&tacheR1_attr,codeEcriture)!=0)){
		printf("Erreur creation tache ecriture fichier \n");
		exit(1);
	}
	//initialisation threadReleve (codeReleve)
	if((createPosixTask("Releve",3,200,PERIODE_MSEC,&tacheReleve,&tacheReleve_attr,codeReleve)!=0)){
		printf("Erreur creation tache de releve temperature \n");
		exit(1);
	}
	//execution de stop() si l'utilisateur presse CTRL+C
	signal(SIGINT,stop);
	pause();
	flagFin = 1;
	//Attente terminaison des threads
	pthread_join(tacheR1,NULL);
	pthread_join(tacheReleve,NULL);
	//destruction sémaphore
	sem_destroy(&semR1);
	//destruction mutex
	pthread_mutex_destroy(&mutexReleve);
	//on ferme le fichier
	fclose(f);
	printf("Fermeture du programme \n");
	return 0;
}

/*
*MOREAU Benjamin
*/
