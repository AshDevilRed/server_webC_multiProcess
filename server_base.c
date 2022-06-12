#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/mman.h>
#include <semaphore.h>

#define CHROOT_DIR "/home/kali/tmp"

char *shared_mem = NULL;

int main(void) {
    // Création de la socket.
    int sock = -1;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("sock");
        exit(EXIT_FAILURE);
    }
    
    // Création des paramètres de la socket.
    struct sockaddr_in sin;
    sin.sin_port = htons(8000);
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_family = AF_INET;

    const int one = 1;
    if ((setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one))) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Attribution des paramètres à la socket.
    if ((bind(sock, (const struct sockaddr *)&sin, sizeof(sin))) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    // Mise en écoute de la socket.
    if ((listen(sock, 1)) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // Création de la sémaphore.
    sem_t sem;
    const int semaphore_shared_mode = 1;
    const unsigned int semaphore_init_value = 1;
    if ((sem_init(&sem, semaphore_shared_mode, semaphore_init_value)) < 0) {
        perror("sem_init");
        exit(EXIT_FAILURE);
    }

    // Boucle qui va traiter les requêtes client.
    while (1) {
        // Accepter la connexion si un client se connecte.
        int client_socket;
        struct sockaddr_in client_address;
        socklen_t client_addr_len = sizeof(client_address); 
        if ((client_socket = accept(sock, (struct sockaddr *)&client_address, &client_addr_len)) < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }
        
        // Recevoir la requête du client.
        char requete_client[512];
        if ((recv(client_socket, requete_client, 512, 0)) < 0) {
            perror("revc");
            exit(EXIT_FAILURE);
        }
        
        // Création de la mémoire partagée anonyme car les deux seul processus du programme l'utilisent.
        size_t page_size = getpagesize();
        if ((shared_mem = (char *)mmap(0, page_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0)) == MAP_FAILED) {
            perror(mmap);
            exit(EXIT_FAILURE);
        }

        // Copie de la requête du client dans la mémoire partagée.
        strncpy(shared_mem, requete_client, page_size);

        pid_t pid;
        uid_t uid = 5926;
        gid_t gid = 5926;

        // Création du processus fils.
        if ((pid = fork()) < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        
        // Processus fils.
        if (pid == 0) {
            // Changment de répertoire vers le sous répertoire chroot.
            chdir(CHROOT_DIR);
            
            // Remplacer la racine du système pour le processus fils par le sous répertoire chroot.
            if ((chroot(CHROOT_DIR)) < 0) {
                perror("chroot");
                exit(EXIT_FAILURE);
            }

            // Changement des identifiants utilisateur et groupe pour l'exécution du fils.
            // Changement du gid en premier car si on change l'uid en avant on aura plus les droits sur le groupe.
            if ((setgid(gid)) < 0) {
                perror("setgid");
                exit(EXIT_FAILURE);
            }
            if ((setuid(uid)) < 0) {
                perror("setgid");
                exit(EXIT_FAILURE);
            }

            // Copie de la mémoire partagée dans la variable requête bloquée par une sémaphore.
            char requete[512];
            if ((sem_wait(&sem)) < 0) {
                perror("sem_wait");
                exit(EXIT_FAILURE);
            }
            strncpy(requete, shared_mem, sizeof(requete));
            if ((sem_post(&sem)) < 0) {
                perror("sem_post");
                exit(EXIT_FAILURE);
            }
            // Parsing de la requête.
            // Vérification que c'est une requête GET.
            if (requete[0] == 'G' && requete[1] == 'E' && requete[2] == 'T') {
                int c = 5; int v = 1; int i = 0;
                char nom_fichier[255] = "0";
                nom_fichier[0] = '.';
                while(v == 1) {
                    if (requete[c] == ' '){
                        v = 0;
                    } else {
                        nom_fichier[i] = requete[c];
                        c++;
                        i++;
                    }
                }

                // Création de la réponse 200 dans le cas ou le fichier existe.
                FILE * fichier;
                unsigned int taille_fichier = 0;
                fichier = fopen(nom_fichier, "r");
                if (fichier != NULL) {
                    struct stat fichier_stat;
                    if (stat(nom_fichier, &fichier_stat) < 0) {
                        perror("stat");
                        exit(EXIT_FAILURE);
                    }
                    taille_fichier = fichier_stat.st_size;
                    char message[50 + taille_fichier];
                    int message_len;
                    char data[taille_fichier];
                    if ((fread(data, taille_fichier, 1, fichier)) == 0 ) {
                        perror("fread");
                        exit(EXIT_FAILURE);
                    }
                    if ((message_len = sprintf(message, "HTTP/1.1 200 Ok\r\nContent-Length: %i\r\n\r\n%s\n", taille_fichier, data)) < 0) {
                        perror("sprintf");
                        exit(EXIT_FAILURE);
                    }
                    if ((fclose(fichier)) < 0) {
                        perror("fclose");
                        exit(EXIT_FAILURE);
                    }

                    // Copie de la réponse avec le contenu du fichier dans la mémoire partagée bloquée par une sémaphore.
                    if ((sem_wait(&sem)) < 0){
                        perror("sem_wait");
                        exit(EXIT_FAILURE);
                    }
                    strncpy(shared_mem, message, page_size);
                    if ((sem_post(&sem)) < 0){
                        perror("sem_post");
                        exit(EXIT_FAILURE);
                    }
                    exit(EXIT_SUCCESS);

                } else {
                    // Création de la réponse 404 dans le cas ou le fichier n'existe pas.
                    char message[50];
                    int message_len;
                    if ((message_len = sprintf(message, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n")) < 0) {
                        perror("sprintf");
                        exit(EXIT_FAILURE);
                    }

                    // Copie de la réponse dans la mémoire partagée bloquée par une sémaphore.
                    if ((sem_wait(&sem)) < 0) {
                        perror("sem_wait");
                        exit(EXIT_FAILURE);
                    }
                    strncpy(shared_mem, message, page_size);
                    if ((sem_post(&sem)) < 0) {
                        perror("sem_post");
                        exit(EXIT_FAILURE);
                    }
                    exit(EXIT_SUCCESS);
               }
            } else {
                exit(EXIT_FAILURE);
            }
        } else {
            // Processus parent.
            // Attendre une réponse du fils (EXIT_FAILURE ou EXIT_SUCCESS).
            int status;
            waitpid(pid, &status, 0);

            // Si le statut est EXIT_SUCCESS envoyer la réponse HTTP au client bloquée par une sémaphore.
            if (status == 0) {
                if ((sem_wait(&sem)) < 0) {
                    perror("sem_wait");
                    exit(EXIT_FAILURE);
                }
                if ((send(client_socket, shared_mem, page_size, 0)) < 0) {
                    perror("send");
                    exit(EXIT_FAILURE);
                }
                if ((sem_post(&sem)) < 0) {
                    perror("sem_post");
                    exit(EXIT_FAILURE);
                }
            }

            // Fermeture de la socket client.
            if ((close(client_socket)) < 0) {
                perror("close");
                exit(EXIT_FAILURE);
            }

            // Libération de la mémoire.
            if ((munmap(shared_mem, page_size)) < 0) {
                perror("munmap");
                exit(EXIT_FAILURE);
            }
        }
    }
    
    // Fermeture de la sémaphore.
    if ((sem_destroy(&sem)) < 0) {
        perror("sem_destroy");
        exit(EXIT_FAILURE);
    }

    // Fermeture de la socket serveur.
    if ((fclose(sock)) < 0) {
        perror("fclose");
        exit(EXIT_FAILURE);
   }
    return EXIT_SUCCESS;
}
