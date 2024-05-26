struct pcmcia_socket_t;
typedef struct pcmcia_socket_t pcmcia_socket_t;

struct pcmcia_socket_t {
    /* Socket number (zero-based) */
    uint8_t socket_num;

    /* Card I/O Read and Write functions. */
    uint8_t (*io_read)(uint16_t port, void* priv);
    uint16_t (*io_readw)(uint16_t port, void* priv);
    void (*io_write)(uint16_t port, uint8_t val, void* priv);
    void (*io_writew)(uint16_t port, uint16_t val, void* priv);

    /* Card Memory Read and Write functions. */
    uint8_t (*mem_read)(uint16_t port, int reg, void* priv);
    uint16_t (*mem_readw)(uint16_t port, int reg, void* priv);
    void (*mem_write)(uint16_t port, uint8_t val, int reg, void* priv);
    void (*mem_writew)(uint16_t port, uint16_t val, int reg, void* priv);

    /* Signals power status change to the card. */
    void (*power_status_change)(pcmcia_socket_t* socket);

    /* Signals card insertion/removal to the socket. */
    void (*card_inserted)(bool inserted, pcmcia_socket_t* socket);

    /* Opaque pointer to card-specific information. */
    void *card_priv;

    /* Opaque pointer to socket-specific information. */
    void *socket_priv;

    /* Card power status. */
    bool powered_on;
};