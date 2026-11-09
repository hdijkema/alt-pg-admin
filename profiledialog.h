#ifndef PROFILEDIALOG_H
#define PROFILEDIALOG_H
#include <QVariantMap>

class QWidget;
namespace ProfileDialog {
// goal: Collect connection settings without touching a server or password vault.
// pre: Call from the GUI thread; profile may be empty for a new connection.
// post: On acceptance, profile contains validated fields and enteredPassword
//       contains the separate secret. Cancellation leaves both arguments intact.
// result: True on acceptance, false on cancellation.
// internals: A local QDialog validates required fields before copying its values.
bool edit(QWidget *parent, bool create, QVariantMap &profile, QString &enteredPassword);
}
#endif
