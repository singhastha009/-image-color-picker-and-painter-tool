#include <gtk/gtk.h>
#include <math.h>

typedef struct
{
    GtkWidget *picture;
    GtkWidget *color_label;
    GtkWidget *color_display;
    GtkWidget *x_entry;
    GtkWidget *y_entry;
    GdkPixbuf *pixbuf;
    char *image_path;
    guint8 selected_r, selected_g, selected_b; 
    gboolean painting;
    gboolean paint_mode;
    int brush_size;

    GList *undo_stack; 
    GList *redo_stack; 
} AppData;

void get_color_at_coordinates(AppData *data, int x, int y);

void update_color_display(AppData *data, guint8 r, guint8 g, guint8 b)
{
    char css[128];
    snprintf(css, sizeof(css), ".custom-color { background-color: rgb(%d, %d, %d); }", r, g, b);

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css); // FIXED: Removed third argument

    gtk_widget_add_css_class(data->color_display, "custom-color");
    gtk_style_context_add_provider_for_display(gtk_widget_get_display(data->color_display),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_USER);

    g_object_unref(provider);
}

void on_get_color_clicked(GtkWidget *widget, gpointer user_data) {
    AppData *data = (AppData *)user_data;
    const char *x_text = gtk_editable_get_text(GTK_EDITABLE(data->x_entry));
    const char *y_text = gtk_editable_get_text(GTK_EDITABLE(data->y_entry));

    if (!x_text || !y_text || !g_ascii_isdigit(x_text[0]) || !g_ascii_isdigit(y_text[0])) {
        g_print("Invalid input. Please enter numeric values.\n");
        return;
    }

    int x = atoi(x_text);
    int y = atoi(y_text);

    // Ensure the coordinates are within the image dimensions
    int img_width = gdk_pixbuf_get_width(data->pixbuf);
    int img_height = gdk_pixbuf_get_height(data->pixbuf);

    if (x < 0 || y < 0 || x >= img_width || y >= img_height) {
        g_print("Manual input: Coordinates out of bounds (%d, %d). Image size: %d x %d\n", x, y, img_width, img_height);
        gtk_label_set_text(GTK_LABEL(data->color_label), "Out of bounds");
        return;
    }

    // Get color at the entered coordinates
    get_color_at_coordinates(data, x, y);
}

void get_color_at_coordinates(AppData *data, int x, int y) {
    if (!data->pixbuf) {
        g_print("No image loaded.\n");
        return;
    }

    int img_width = gdk_pixbuf_get_width(data->pixbuf);
    int img_height = gdk_pixbuf_get_height(data->pixbuf);
    int widget_width = gtk_widget_get_width(data->picture);
    int widget_height = gtk_widget_get_height(data->picture);

    // Get aspect ratio of the image
    double img_aspect = (double)img_width / img_height;
    double widget_aspect = (double)widget_width / widget_height;

    int px = 0, py = 0;

    if (img_aspect > widget_aspect) {
        // Image is wider than widget: black bars at top/bottom
        double scale = (double)widget_width / img_width;
        int scaled_height = (int)(img_height * scale);
        int y_offset = (widget_height - scaled_height) / 2;

        px = (x * img_width) / widget_width;
        py = ((y - y_offset) * img_height) / scaled_height;
    } else {
        // Image is taller than widget: black bars at left/right
        double scale = (double)widget_height / img_height;
        int scaled_width = (int)(img_width * scale);
        int x_offset = (widget_width - scaled_width) / 2;

        px = ((x - x_offset) * img_width) / scaled_width;
        py = (y * img_height) / widget_height;
    }

    // Ensure coordinates are within valid range
    if (px < 0 || py < 0 || px >= img_width || py >= img_height) {
        g_print("Click: Coordinates out of bounds (%d, %d). Image size: %d x %d\n", px, py, img_width, img_height);
        gtk_label_set_text(GTK_LABEL(data->color_label), "Out of bounds");
        return;
    }

    int rowstride = gdk_pixbuf_get_rowstride(data->pixbuf);
    int n_channels = gdk_pixbuf_get_n_channels(data->pixbuf);
    guchar *pixels = gdk_pixbuf_get_pixels(data->pixbuf);

    // Get pixel color at mapped coordinates
    guchar *pixel = pixels + py * rowstride + px * n_channels;
    guint8 r = pixel[0], g = pixel[1], b = pixel[2];

    // Store selected color
    data->selected_r = r;
    data->selected_g = g;
    data->selected_b = b;

    char color_text[64];
    snprintf(color_text, sizeof(color_text), "RGB: R=%d, G=%d, B=%d", r, g, b);
    gtk_label_set_text(GTK_LABEL(data->color_label), color_text);

    update_color_display(data, r, g, b);

    g_print("Clicked on (Widget: %d, %d) -> (Image: %d, %d) -> RGB(%d, %d, %d)\n",
            x, y, px, py, r, g, b);
}

void save_undo_state(AppData *data)
{
    if (!data->pixbuf)
        return;

    // Create a copy of the current image
    GdkPixbuf *pixbuf_copy = gdk_pixbuf_copy(data->pixbuf);

    // Push it onto the undo stack
    data->undo_stack = g_list_prepend(data->undo_stack, pixbuf_copy);

    // Clear redo stack (since redo is only valid after an undo)
    g_list_free_full(data->redo_stack, g_object_unref);
    data->redo_stack = NULL;

    g_print("Undo state saved. Stack size: %d\n", g_list_length(data->undo_stack));
}

void paint_brush(AppData *data, int x, int y)
{
    if (!data->pixbuf)
        return;

    // Save the image before making any modifications
    save_undo_state(data);

    int img_width = gdk_pixbuf_get_width(data->pixbuf);
    int img_height = gdk_pixbuf_get_height(data->pixbuf);
    int widget_width = gtk_widget_get_width(data->picture);
    int widget_height = gtk_widget_get_height(data->picture);

    // Adjust for scaling
    double img_aspect = (double)img_width / img_height;
    double widget_aspect = (double)widget_width / widget_height;
    int px, py;

    if (img_aspect > widget_aspect)
    {
        double scale = (double)widget_width / img_width;
        int scaled_height = (int)(img_height * scale);
        int y_offset = (widget_height - scaled_height) / 2;
        px = (x * img_width) / widget_width;
        py = ((y - y_offset) * img_height) / scaled_height;
    }
    else
    {
        double scale = (double)widget_height / img_height;
        int scaled_width = (int)(img_width * scale);
        int x_offset = (widget_width - scaled_width) / 2;
        px = ((x - x_offset) * img_width) / scaled_width;
        py = (y * img_height) / widget_height;
    }

    if (px < 0 || py < 0 || px >= img_width || py >= img_height)
    {
        return;
    }

    int rowstride = gdk_pixbuf_get_rowstride(data->pixbuf);
    int n_channels = gdk_pixbuf_get_n_channels(data->pixbuf);
    guchar *pixels = gdk_pixbuf_get_pixels(data->pixbuf);

    int radius = data->brush_size / 2;

    for (int dy = -radius; dy <= radius; dy++)
    {
        for (int dx = -radius; dx <= radius; dx++)
        {
            if (sqrt(dx * dx + dy * dy) <= radius)
            {
                int nx = px + dx;
                int ny = py + dy;

                if (nx >= 0 && ny >= 0 && nx < img_width && ny < img_height)
                {
                    int offset = ny * rowstride + nx * n_channels;
                    pixels[offset] = data->selected_r;
                    pixels[offset + 1] = data->selected_g;
                    pixels[offset + 2] = data->selected_b;
                }
            }
        }
    }

    // Refresh the displayed image
    GdkTexture *texture = gdk_texture_new_for_pixbuf(data->pixbuf);
    gtk_picture_set_paintable(GTK_PICTURE(data->picture), GDK_PAINTABLE(texture));

    g_print("Painted at (Widget: %d, %d) -> (Image: %d, %d) with RGB(%d, %d, %d)\n",
            x, y, px, py, data->selected_r, data->selected_g, data->selected_b);
}

void on_undo_clicked(GtkWidget *widget, gpointer user_data)
{
    AppData *data = (AppData *)user_data;

    if (!data->undo_stack)
    {
        g_print("Undo stack is empty.\n");
        return;
    }

    // Save current state to redo stack
    data->redo_stack = g_list_prepend(data->redo_stack, gdk_pixbuf_copy(data->pixbuf));

    // Pop from undo stack
    GdkPixbuf *previous_state = data->undo_stack->data;
    data->undo_stack = g_list_delete_link(data->undo_stack, data->undo_stack);

    // Apply the previous state
    g_object_unref(data->pixbuf);
    data->pixbuf = previous_state;

    // Refresh image
    GdkTexture *texture = gdk_texture_new_for_pixbuf(data->pixbuf);
    gtk_picture_set_paintable(GTK_PICTURE(data->picture), GDK_PAINTABLE(texture));

    g_print("Undo applied. Remaining stack size: %d\n", g_list_length(data->undo_stack));
}

void on_redo_clicked(GtkWidget *widget, gpointer user_data)
{
    AppData *data = (AppData *)user_data;

    if (!data->redo_stack)
    {
        g_print("Redo stack is empty.\n");
        return;
    }

    // Save current state to undo stack
    data->undo_stack = g_list_prepend(data->undo_stack, gdk_pixbuf_copy(data->pixbuf));

    // Pop from redo stack
    GdkPixbuf *next_state = data->redo_stack->data;
    data->redo_stack = g_list_delete_link(data->redo_stack, data->redo_stack);

    // Apply the redo state
    g_object_unref(data->pixbuf);
    data->pixbuf = next_state;

    // Refresh image
    GdkTexture *texture = gdk_texture_new_for_pixbuf(data->pixbuf);
    gtk_picture_set_paintable(GTK_PICTURE(data->picture), GDK_PAINTABLE(texture));

    g_print("Redo applied. Remaining redo stack size: %d\n", g_list_length(data->redo_stack));
}

gboolean on_mouse_press(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data) {
    AppData *data = (AppData *)user_data;

    if (data->paint_mode) {
        // If paint mode is enabled, start painting
        data->painting = TRUE;
        paint_brush(data, (int)x, (int)y);
    } else {
        // If paint mode is OFF, get color from clicked position
        get_color_at_coordinates(data, (int)x, (int)y);
    }

    return TRUE;
}

gboolean on_mouse_release(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data)
{
    AppData *data = (AppData *)user_data;
    data->painting = FALSE;
    return TRUE;
}

gboolean on_mouse_motion(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer user_data)
{
    AppData *data = (AppData *)user_data;

    if (data->painting && data->paint_mode)
    { // Only allow painting if mode is active
        paint_brush(data, (int)x, (int)y);
    }

    return TRUE;
}

void paint_pixel(AppData *data, int x, int y)
{
    if (!data->pixbuf)
    {
        g_print("No image loaded.\n");
        return;
    }

    int img_width = gdk_pixbuf_get_width(data->pixbuf);
    int img_height = gdk_pixbuf_get_height(data->pixbuf);

    if (x < 0 || y < 0 || x >= img_width || y >= img_height)
    {
        g_print("Coordinates out of bounds: (%d, %d)\n", x, y);
        return;
    }

    int rowstride = gdk_pixbuf_get_rowstride(data->pixbuf);
    int n_channels = gdk_pixbuf_get_n_channels(data->pixbuf);
    guchar *pixels = gdk_pixbuf_get_pixels(data->pixbuf);

    // Paint the pixel with the selected color
    guchar *pixel = pixels + y * rowstride + x * n_channels;
    pixel[0] = data->selected_r; // Red
    pixel[1] = data->selected_g; // Green
    pixel[2] = data->selected_b; // Blue

    // Update the displayed image
    GdkTexture *texture = gdk_texture_new_for_pixbuf(data->pixbuf);
    gtk_picture_set_paintable(GTK_PICTURE(data->picture), GDK_PAINTABLE(texture));

    g_print("Painted pixel at (%d, %d) with RGB(%d, %d, %d)\n",
            x, y, data->selected_r, data->selected_g, data->selected_b);
}

void load_image(AppData *data)
{
    if (!data->image_path)
    {
        g_print("No image path provided.\n");
        return;
    }

    GError *error = NULL;
    data->pixbuf = gdk_pixbuf_new_from_file(data->image_path, &error);
    if (!data->pixbuf)
    {
        g_print("Failed to load image: %s\n", error->message);
        g_error_free(error);
        return;
    }

    GdkTexture *texture = gdk_texture_new_for_pixbuf(data->pixbuf);
    gtk_picture_set_paintable(GTK_PICTURE(data->picture), GDK_PAINTABLE(texture));
}


void on_paint_button_clicked(GtkWidget *widget, gpointer user_data)
{
    AppData *data = (AppData *)user_data;

    // Toggle the paint mode
    data->paint_mode = !data->paint_mode;

    if (data->paint_mode)
    {
        gtk_button_set_label(GTK_BUTTON(widget), "Stop Painting");
        g_print("Paint mode activated.\n");
    }
    else
    {
        gtk_button_set_label(GTK_BUTTON(widget), "Paint");
        data->painting = FALSE; // Ensure painting stops if toggled off
        g_print("Paint mode deactivated.\n");
    }
}

void on_brush_size_changed(GtkRange *range, gpointer user_data)
{
    AppData *data = (AppData *)user_data;
    data->brush_size = (int)gtk_range_get_value(range);
    g_print("Brush size: %d\n", data->brush_size);
}

void on_save_clicked(GtkWidget *widget, gpointer user_data)
{
    AppData *data = (AppData *)user_data;

    if (!data->pixbuf || !data->image_path)
    {
        g_print("No image loaded to save.\n");
        return;
    }

    // Get directory of the original image
    gchar *dir_path = g_path_get_dirname(data->image_path);

    // Create new filename: "painted_image.png" in the same directory
    gchar *save_path = g_build_filename(dir_path, "painted_image.png", NULL);

    GError *error = NULL;

    // Save the pixbuf as a PNG file
    if (!gdk_pixbuf_save(data->pixbuf, save_path, "png", &error, NULL))
    {
        g_print("Failed to save image: %s\n", error->message);
        g_error_free(error);
    }
    else
    {
        g_print("Image saved successfully: %s\n", save_path);
    }

    // Free allocated memory
    g_free(save_path);
    g_free(dir_path);
}

void on_app_activate(GtkApplication *app, gpointer user_data)
{
    AppData *data = (AppData *)user_data;

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Pixel Color Picker");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 800);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_window_set_child(GTK_WINDOW(window), box);

    data->picture = gtk_picture_new();
    gtk_box_append(GTK_BOX(box), data->picture);

    load_image(data);

    GtkWidget *coord_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_append(GTK_BOX(box), coord_box);

    GtkWidget *x_label = gtk_label_new("X:");
    gtk_box_append(GTK_BOX(coord_box), x_label);
    data->x_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(data->x_entry), "Enter X");
    gtk_box_append(GTK_BOX(coord_box), data->x_entry);

    GtkWidget *y_label = gtk_label_new("Y:");
    gtk_box_append(GTK_BOX(coord_box), y_label);
    data->y_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(data->y_entry), "Enter Y");
    gtk_box_append(GTK_BOX(coord_box), data->y_entry);

    GtkWidget *get_paint_button = gtk_button_new_with_label("Paint");
    gtk_box_append(GTK_BOX(coord_box), get_paint_button);
    g_signal_connect(get_paint_button, "clicked", G_CALLBACK(on_paint_button_clicked), data);


    GtkGesture *click_gesture = gtk_gesture_click_new();
    gtk_widget_add_controller(data->picture, GTK_EVENT_CONTROLLER(click_gesture));
    g_signal_connect(click_gesture, "pressed", G_CALLBACK(on_mouse_press), data);
    g_signal_connect(click_gesture, "released", G_CALLBACK(on_mouse_release), data);

    GtkEventController *motion_controller = gtk_event_controller_motion_new();
    gtk_widget_add_controller(data->picture, motion_controller);
    g_signal_connect(motion_controller, "motion", G_CALLBACK(on_mouse_motion), data);

    GtkWidget *get_brush_button = gtk_button_new_with_label("Brush");
    GtkWidget *brush_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 50, 1);
    gtk_range_set_value(GTK_RANGE(brush_slider), 10);
    gtk_box_append(GTK_BOX(box), brush_slider);
    g_signal_connect(brush_slider, "value-changed", G_CALLBACK(on_brush_size_changed), data);

    GtkWidget *get_redo_button = gtk_button_new_with_label("Redo");
    gtk_box_append(GTK_BOX(coord_box), get_redo_button);
    g_signal_connect(get_redo_button, "clicked", G_CALLBACK(on_redo_clicked), data);

    GtkWidget *get_undo_button = gtk_button_new_with_label("Undo");
    gtk_box_append(GTK_BOX(coord_box), get_undo_button);
    g_signal_connect(get_undo_button, "clicked", G_CALLBACK(on_undo_clicked), data);

    GtkWidget *get_save_button = gtk_button_new_with_label("Save");
    gtk_box_append(GTK_BOX(coord_box), get_save_button);
    g_signal_connect(get_save_button, "clicked", G_CALLBACK(on_save_clicked), data);

    GtkWidget *get_color_button = gtk_button_new_with_label("Get Color");
    gtk_box_append(GTK_BOX(coord_box), get_color_button);
    g_signal_connect(get_color_button, "clicked", G_CALLBACK(on_get_color_clicked), data);

    data->color_label = gtk_label_new("Click on the image or enter coordinates.");
    gtk_box_append(GTK_BOX(box), data->color_label);

    data->color_display = gtk_frame_new(NULL);
    gtk_widget_set_size_request(data->color_display, 200, 50);
    gtk_box_append(GTK_BOX(box), data->color_display);

    gtk_window_present(GTK_WINDOW(window));
}


void on_open(GtkApplication *app, GFile **files, gint n_files, gchar *hint, gpointer user_data)
{
    AppData *data = (AppData *)user_data;
    if (n_files > 0)
    {
        gchar *file_path = g_file_get_path(files[0]);
        data->image_path = g_strdup(file_path);
        g_free(file_path);
    }
    on_app_activate(app, data);
}

int main(int argc, char *argv[])
{
    GtkApplication *app = gtk_application_new("com.example.colorpicker", G_APPLICATION_HANDLES_OPEN);
    AppData data = {0};

    if (argc > 1)
    {
        data.image_path = argv[1];
    }
    else
    {
        g_print("Usage: %s <image-file>\n", argv[0]);
        return 1;
    }

    g_signal_connect(app, "activate", G_CALLBACK(on_app_activate), &data);
    g_signal_connect(app, "open", G_CALLBACK(on_open), &data);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}