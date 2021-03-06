INCLUDEPATH += $$PWD

HEADERS +=  $$PWD/transfers_widget.h \
            $$PWD/transfer_model.h \
            $$PWD/transfermodel_item.h \
            $$PWD/transferlist_delegate.h \
    transfers/peermodel.h

SOURCES +=  $$PWD/transfers_widget.cpp \
            $$PWD/transfer_model.cpp \
            $$PWD/transfermodel_item.cpp \
            $$PWD/transferlist_delegate.cpp \
    transfers/peermodel.cpp

FORMS   += $$PWD/transfers_widget.ui
