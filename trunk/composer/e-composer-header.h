#ifndef E_COMPOSER_HEADER_H
#define E_COMPOSER_HEADER_H

#include "e-composer-common.h"

/* Standard GObject macros */
#define E_TYPE_COMPOSER_HEADER \
	(e_composer_header_get_type ())
#define E_COMPOSER_HEADER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_COMPOSER_HEADER, EComposerHeader))
#define E_COMPOSER_HEADER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((obj), E_TYPE_COMPOSER_HEADER, EComposerHeaderClass))
#define E_IS_COMPOSER_HEADER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_COMPOSER_HEADER))
#define E_IS_COMPOSER_HEADER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_COMPOSER_HEADER))
#define E_COMPOSER_HEADER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_COMPOSER_HEADER, EComposerHeaderClass))

G_BEGIN_DECLS

typedef struct _EComposerHeader EComposerHeader;
typedef struct _EComposerHeaderClass EComposerHeaderClass;
typedef struct _EComposerHeaderPrivate EComposerHeaderPrivate;

struct _EComposerHeader {
	GObject parent;
	GtkWidget *title_widget;
	GtkWidget *input_widget;
	EComposerHeaderPrivate *priv;
};

struct _EComposerHeaderClass {
	GObjectClass parent_class;
};

GType		e_composer_header_get_type	(void);
gchar *		e_composer_header_get_label	(EComposerHeader *header);
gboolean	e_composer_header_get_visible	(EComposerHeader *header);
void		e_composer_header_set_visible	(EComposerHeader *header,
						 gboolean visible);
void		e_composer_header_set_title_tooltip
						(EComposerHeader *header,
						 const gchar *tooltip);
void		e_composer_header_set_input_tooltip
						(EComposerHeader *header,
						 const gchar *tooltip);

G_END_DECLS

#endif /* E_COMPOSER_HEADER_H */
