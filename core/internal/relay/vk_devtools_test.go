package relay

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/gorilla/websocket"
)

// A browser that needs a while for every command must not run out of one shared allowance.
func TestDevToolsDeadlineCoversEachCommand(t *testing.T) {
	upgrader := websocket.Upgrader{}
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		conn, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			return
		}
		defer conn.Close()
		for {
			var request struct {
				ID int `json:"id"`
			}
			if conn.ReadJSON(&request) != nil {
				return
			}
			time.Sleep(150 * time.Millisecond)
			_ = conn.WriteJSON(map[string]any{"id": request.ID, "result": map[string]any{}})
		}
	}))
	defer server.Close()
	conn, _, err := websocket.DefaultDialer.Dial("ws"+strings.TrimPrefix(server.URL, "http"), nil)
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Close()
	c := devTools{conn: conn, wait: 400 * time.Millisecond}
	for i := 0; i < 4; i++ {
		if _, err := c.call("Page.enable", map[string]any{}); err != nil {
			t.Fatalf("command %d: %v", i, err)
		}
	}
	c.wait = 10 * time.Millisecond
	if _, err := c.call("Page.enable", map[string]any{}); !devToolsTimeout(err) {
		t.Fatalf("a silent browser should time out, got %v", err)
	}
}
