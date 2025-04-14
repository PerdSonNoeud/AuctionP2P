#include "message.h"

/// @brief Permet de remplir un buffer avec un message
/// @param iMess message a copier dans le buffer
/// @param iSize taille du message
/// @param iOffset position de depart dans le buffer
/// @param oBuffer buffer de destination
void remplir_buffer(void* iMess, int iSize, int* iOffset, void* oBuffer);


/// @brief Convertit une structure message en buffer
/// @param iMess message a convertir
/// @return retourne un buffer contenant le message
void* struct_to_buffer(struct message* iMess);
