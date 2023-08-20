typedef struct DASHContext {
    // ... [Diğer değişkenler]

    // Anahtar listesi için eklenen değişkenler
    DecryptionKey decryption_keys[MAX_KEYS];
    int num_keys;
} DASHContext;
