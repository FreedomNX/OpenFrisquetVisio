std::vector<byte> reply;
bool pending = false;
uint8_t owner = 0;
RadioTrameHeader expected;
bool beginObservation(uint8_t id, const RadioTrameHeader& header) {
    if (pending) return false;
    pending = true; owner = id; expected = header; return true;
}
bool ownsObservation(uint8_t id) const { return pending && owner == id; }
bool observationReady(uint8_t id) const { return ownsObservation(id); }
void cancelObservation() { pending = false; }
int takeObservation(uint8_t id, byte* output, size_t& length) {
    assert(ownsObservation(id)); pending = false;
    return receiveExpected(expected.idExpediteur, expected.idDestinataire,
        expected.idAssociation, expected.idMessage, expected.idReception,
        expected.type, output, length, 15, true);
}
int receiveExpected(byte sender, byte destination, byte association, byte message,
                    byte reception, byte type, byte* output, size_t& length,
                    byte, bool) {
    if (reply.size()<sizeof(RadioTrameHeader)) return -1;
    // Utiliser les critères de sélection du transport, qui ne vérifie pas l’association.
    (void)association;
    const auto* h=reinterpret_cast<const RadioTrameHeader*>(reply.data());
    if (h->idExpediteur!=sender || h->idDestinataire!=destination ||
        h->idMessage!=message ||
        h->idReception!=reception || h->type!=type) return -1;
    length=reply.size();
    memcpy(output, reply.data(), length);
    return RADIOLIB_ERR_NONE;
}
