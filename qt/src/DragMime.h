#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QList>
#include <QMimeData>
#include <QString>

namespace qhostty {

inline constexpr auto tabMimeType = "application/x-qhostty-tab";
inline constexpr auto surfaceMimeType = "application/x-qhostty-surface";

// A drag carries a pointer, which is valid only in the process that started
// the drag. The receiver must also make sure that the object still exists.
inline void setDragPointer(QMimeData& mime,
                           const char* type,
                           const void* pointer) {
  mime.setData(QString::fromLatin1(type),
               QByteArray::number(QCoreApplication::applicationPid()) + ':' +
                   QByteArray::number(reinterpret_cast<quintptr>(pointer)));
}

template <typename T>
T* dragPointer(const QMimeData& mime, const char* type) {
  const QList<QByteArray> parts =
      mime.data(QString::fromLatin1(type)).split(':');
  if (parts.size() != 2 ||
      parts.at(0).toLongLong() != QCoreApplication::applicationPid()) {
    return nullptr;
  }
  return reinterpret_cast<T*>(
      static_cast<quintptr>(parts.at(1).toULongLong()));
}

}  // namespace qhostty
