package cf

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"net/url"
	"sort"
	"strings"
	"time"
)

// App is a Cloudflare Access application that can be imported as a device.
type App struct {
	Name   string `json:"name"`
	Domain string `json:"domain"`
	Type   string `json:"type"`
}

var apiBase = "https://api.cloudflare.com/client/v4"

type apiResponse[T any] struct {
	Success bool `json:"success"`
	Errors  []struct {
		Message string `json:"message"`
	} `json:"errors"`
	Result T `json:"result"`
}

func apiGet[T any](ctx context.Context, token, path string) (T, error) {
	var zero T
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, apiBase+path, nil)
	if err != nil {
		return zero, err
	}
	req.Header.Set("Authorization", "Bearer "+token)
	resp, err := (&http.Client{Timeout: 15 * time.Second}).Do(req)
	if err != nil {
		return zero, err
	}
	defer resp.Body.Close()
	var r apiResponse[T]
	if err := json.NewDecoder(resp.Body).Decode(&r); err != nil {
		return zero, fmt.Errorf("cloudflare api: %v", err)
	}
	if !r.Success {
		if len(r.Errors) > 0 {
			return zero, errors.New(r.Errors[0].Message)
		}
		return zero, fmt.Errorf("cloudflare api: HTTP %d", resp.StatusCode)
	}
	return r.Result, nil
}

// ListApps lists the Access applications of every account the API token can read. The token is used
// for this call only and never stored. Needs "Access: Apps and Policies Read" and "Account Settings Read".
func ListApps(ctx context.Context, token string) ([]App, error) {
	token = strings.TrimSpace(token)
	if token == "" {
		return nil, errors.New("enter a Cloudflare API token")
	}
	accounts, err := apiGet[[]struct {
		ID string `json:"id"`
	}](ctx, token, "/accounts?per_page=50")
	if err != nil {
		return nil, err
	}
	var out []App
	for _, a := range accounts {
		apps, err := apiGet[[]App](ctx, token, "/accounts/"+url.PathEscape(a.ID)+"/access/apps?per_page=100")
		if err != nil {
			return nil, err
		}
		for _, app := range apps {
			if app.Domain == "" || strings.HasPrefix(app.Type, "app_launcher") || app.Type == "warp" {
				continue
			}
			app.Domain = strings.SplitN(strings.TrimPrefix(app.Domain, "https://"), "/", 2)[0]
			out = append(out, app)
		}
	}
	sort.Slice(out, func(i, j int) bool { return strings.ToLower(out[i].Name) < strings.ToLower(out[j].Name) })
	return out, nil
}
