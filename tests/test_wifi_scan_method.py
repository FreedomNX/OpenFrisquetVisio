"""Check that the station scans every channel and joins the strongest access point (mesh networks)."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/OFVNetworkManager.cpp').read_text()

begin = source[source.index('void OFVNetworkManager::begin('):source.index('void OFVNetworkManager::attachEvents(')]
scan = begin.index('WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);')
sort = begin.index('WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);')
connect = begin.index('startConnect();')
# Le mode de balayage doit être réglé avant la première connexion (WiFi.begin dans startConnect).
assert scan < connect and sort < connect, 'balayage à régler avant startConnect()'
# Une reconnexion réutilise ces réglages : aucun autre appel ne doit revenir au balayage rapide.
assert 'WIFI_FAST_SCAN' not in source
print('OK: balayage de tous les canaux et choix de la borne au meilleur signal avant la connexion')
