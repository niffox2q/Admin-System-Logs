## [Admin System] Logs
Логирование наказаний от администраторов в дискорд через вебхук

### Логирует следующее:
---
- Выдача любых наказаний игроку (бан,мут,гаг,silence)
- Снятие любых наказаний игрока (бан,мут,гаг,silence)
- Выдача любых наказаний игроку в оффлайн (бан,мут,гаг,silence)
- P.S Снятие наказаний в оффлайн не работает.
---

### Требования
---
- [Metamod:Source](https://www.sourcemm.net/downloads.php?branch=dev)
- [Utils](https://github.com/Pisex/cs2-menus/releases/latest)
- [AdminSystem](https://github.com/Pisex/cs2-admin_system/releases/latest)
---
### Пример конфигурации
```ini
"Config"
{
    "webhook"   "ссылка на вебхук"
    "consoleDontSend" "0" // Отправлять ли сообщение если администратор выдавший наказание - консоль?
}
```
Перезагрузить конфигурацию: `mm_as_logs_reload`

