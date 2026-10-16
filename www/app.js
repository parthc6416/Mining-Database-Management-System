/* Mining Database Management System - browser front end.
 * Talks to the C server in server.c over the small JSON API under /api/. */

(function () {
  "use strict";

  var meta = null;          /* entity / report / DDL definitions from the server */
  var route = { kind: "dashboard", index: 0 };
  var modalSubmit = null;   /* handler for the currently open modal */

  /* ------------------------------------------------------------------ *
   * Small helpers
   * ------------------------------------------------------------------ */

  function $(id) { return document.getElementById(id); }

  function el(tag, className, text) {
    var n = document.createElement(tag);
    if (className) n.className = className;
    if (text !== undefined && text !== null) n.textContent = String(text);
    return n;
  }

  function clear(node) {
    while (node.firstChild) node.removeChild(node.firstChild);
    return node;
  }

  function encodeForm(params) {
    var parts = [];
    Object.keys(params).forEach(function (k) {
      var v = params[k];
      if (v === undefined || v === null) v = "";
      parts.push(encodeURIComponent(k) + "=" + encodeURIComponent(v));
    });
    return parts.join("&");
  }

  /* Resolves to the parsed body; rejects with an Error carrying .status. */
  function request(method, path, params) {
    var url = path;
    var opts = { method: method, headers: {} };

    if (method === "GET") {
      var qs = encodeForm(params || {});
      if (qs) url += "?" + qs;
    } else {
      opts.headers["Content-Type"] = "application/x-www-form-urlencoded";
      opts.body = encodeForm(params || {});
    }

    return fetch(url, opts).then(function (res) {
      return res.text().then(function (text) {
        var data;
        try { data = JSON.parse(text); }
        catch (e) { data = { ok: false, error: text || ("HTTP " + res.status) }; }
        if (!res.ok || data.ok === false) {
          var err = new Error(data.error || ("HTTP " + res.status));
          err.status = res.status;
          throw err;
        }
        return data;
      });
    });
  }

  function apiGet(path, params) { return request("GET", path, params); }
  function apiPost(path, params) { return request("POST", path, params); }

  /* A lost session sends the user back to the login screen. */
  function handleError(err) {
    if (err && err.status === 401) {
      showLogin("Your database session ended. Please connect again.");
      return;
    }
    toast(err && err.message ? err.message : "Unexpected error.", "err");
  }

  function toast(message, kind) {
    var box = $("toasts");
    var t = el("div", "toast " + (kind || ""));
    t.appendChild(el("span", "ti", kind === "err" ? "✕" : kind === "ok" ? "✓" : "•"));
    t.appendChild(el("span", null, message));
    box.appendChild(t);
    setTimeout(function () {
      t.style.transition = "opacity .25s";
      t.style.opacity = "0";
      setTimeout(function () { if (t.parentNode) box.removeChild(t); }, 250);
    }, kind === "err" ? 7000 : 3600);
  }

  function fmtNumber(v) {
    if (v === null || v === undefined || v === "") return v;
    var n = Number(v);
    if (!isFinite(n) || String(v).trim() === "") return v;
    return n.toLocaleString(undefined, { maximumFractionDigits: 2 });
  }

  function isNumeric(v) {
    return v !== null && v !== "" && isFinite(Number(v));
  }

  /* ------------------------------------------------------------------ *
   * Modal
   * ------------------------------------------------------------------ */

  function openModal(title, buildBody, onSubmit, submitLabel, submitClass) {
    $("modal-title").textContent = title;
    clear($("modal-body"));
    buildBody($("modal-body"));
    $("modal-error").hidden = true;
    $("modal-error").textContent = "";

    var btn = $("modal-submit");
    btn.textContent = submitLabel || "Save";
    btn.className = "btn " + (submitClass || "primary");
    btn.disabled = false;

    modalSubmit = onSubmit;
    $("modal-backdrop").hidden = false;

    var first = $("modal-body").querySelector("input:not([readonly]), select, textarea");
    if (first) first.focus();
  }

  function closeModal() {
    $("modal-backdrop").hidden = true;
    modalSubmit = null;
  }

  function modalError(message) {
    var p = $("modal-error");
    p.textContent = message;
    p.hidden = false;
  }

  $("modal-close").addEventListener("click", closeModal);
  $("modal-cancel").addEventListener("click", closeModal);
  $("modal-backdrop").addEventListener("mousedown", function (e) {
    if (e.target === $("modal-backdrop")) closeModal();
  });
  document.addEventListener("keydown", function (e) {
    if (e.key === "Escape" && !$("modal-backdrop").hidden) closeModal();
  });

  $("modal-form").addEventListener("submit", function (e) {
    e.preventDefault();
    if (!modalSubmit) return;
    var btn = $("modal-submit");
    btn.disabled = true;
    var done = modalSubmit();
    if (done && typeof done.then === "function") {
      done.then(function () { closeModal(); })
          .catch(function (err) {
            btn.disabled = false;
            if (err && err.status === 401) { closeModal(); handleError(err); }
            else modalError(err && err.message ? err.message : "Operation failed.");
          });
    } else {
      btn.disabled = false;
    }
  });

  /* Builds one labelled input for an entity field. */
  function fieldInput(parent, field, value, readOnly) {
    var wrap = el("div", "field");
    var id = "f_" + field.column;
    var label = el("label", null, field.label);
    label.setAttribute("for", id);
    var hint = el("span", "type-hint",
      field.type === "N" ? "number" : field.type === "D" ? "date" : "text");
    label.appendChild(hint);
    wrap.appendChild(label);

    var input = document.createElement("input");
    input.id = id;
    input.dataset.column = field.column;
    if (field.type === "D") {
      input.type = "date";
    } else if (field.type === "N") {
      input.type = "text";
      input.inputMode = "decimal";
      input.setAttribute("pattern", "-?[0-9]*\\.?[0-9]*");
    } else {
      input.type = "text";
      input.maxLength = 500;
    }
    input.value = value === null || value === undefined ? "" : String(value);
    if (readOnly) input.readOnly = true;
    wrap.appendChild(input);

    if (readOnly) wrap.appendChild(el("small", null, "Primary key — cannot be changed here."));
    parent.appendChild(wrap);
    return input;
  }

  /* ------------------------------------------------------------------ *
   * Login
   * ------------------------------------------------------------------ */

  function showLogin(message) {
    $("app-view").hidden = true;
    $("login-view").hidden = false;
    closeModal();
    var p = $("login-error");
    if (message) { p.textContent = message; p.hidden = false; }
    else { p.textContent = ""; p.hidden = true; }
    $("connect-btn").disabled = false;
    $("connect-btn").textContent = "Connect";
  }

  function showApp(user, dsn) {
    $("login-view").hidden = true;
    $("app-view").hidden = false;
    $("conn-user").textContent = user || "(connected)";
    $("conn-dsn").textContent = dsn || "";
  }

  $("use-connstr").addEventListener("change", function () {
    var on = this.checked;
    $("connstr-field").hidden = !on;
    $("dsn").closest(".field").hidden = on;
    $("user").closest(".field").hidden = on;
    $("password").closest(".field").hidden = on;
    (on ? $("connstr") : $("dsn")).focus();
  });

  $("show-dsns").addEventListener("click", function () {
    var box = $("dsn-list");
    if (!box.hidden) { box.hidden = true; return; }
    box.hidden = false;
    clear(box).appendChild(el("div", "empty", "Reading ODBC registry…"));

    apiGet("/api/datasources").then(function (data) {
      clear(box);
      box.appendChild(el("h4", null, "Data sources (click to use)"));
      if (!data.dsns.length) {
        box.appendChild(el("div", "empty", "No DSN configured. Create one in odbcad32.exe."));
      } else {
        data.dsns.forEach(function (d) {
          var b = el("button", null, d.name + "  —  " + d.driver);
          b.type = "button";
          b.addEventListener("click", function () {
            $("use-connstr").checked = false;
            $("use-connstr").dispatchEvent(new Event("change"));
            $("dsn").value = d.name;
            $("user").focus();
          });
          box.appendChild(b);
        });
      }
      box.appendChild(el("h4", null, "Installed drivers"));
      if (!data.drivers.length) {
        box.appendChild(el("div", "empty", "No ODBC drivers found."));
      } else {
        data.drivers.forEach(function (name) {
          box.appendChild(el("div", "drv", name));
        });
      }
    }).catch(function (err) {
      clear(box).appendChild(el("div", "empty", err.message));
    });
  });

  $("login-form").addEventListener("submit", function (e) {
    e.preventDefault();
    var btn = $("connect-btn");
    var useConn = $("use-connstr").checked;
    var payload = useConn
      ? { connstr: $("connstr").value.trim() }
      : { dsn: $("dsn").value.trim(), user: $("user").value, password: $("password").value };

    btn.disabled = true;
    btn.textContent = "Connecting…";
    $("login-error").hidden = true;

    apiPost("/api/connect", payload).then(function (data) {
      $("password").value = "";
      $("connstr").value = "";
      showApp(data.user, data.dsn);
      toast("Connected to Oracle through ODBC.", "ok");
      return loadMeta();
    }).then(function () {
      go({ kind: "dashboard", index: 0 });
    }).catch(function (err) {
      btn.disabled = false;
      btn.textContent = "Connect";
      var p = $("login-error");
      p.textContent = err.message || "Connection failed.";
      p.hidden = false;
    });
  });

  $("disconnect-btn").addEventListener("click", function () {
    apiPost("/api/disconnect").catch(function () { /* leaving anyway */ })
      .then(function () {
        showLogin("Disconnected.");
        $("dsn-list").hidden = true;
      });
  });

  /* ------------------------------------------------------------------ *
   * Navigation
   * ------------------------------------------------------------------ */

  var NAV_ICONS = ["⛰", "💎", "👷", "🚜", "📦", "🛡"];

  function buildNav() {
    var nav = clear($("nav"));

    nav.appendChild(el("div", "nav-group", "Overview"));
    nav.appendChild(navItem("📊", "Dashboard", { kind: "dashboard", index: 0 }));

    nav.appendChild(el("div", "nav-group", "Tables"));
    meta.entities.forEach(function (ent, i) {
      nav.appendChild(navItem(NAV_ICONS[i] || "▪", ent.title, { kind: "entity", index: i }));
    });

    nav.appendChild(el("div", "nav-group", "Queries"));
    nav.appendChild(navItem("📈", "Reports / DQL", { kind: "reports", index: 0 }));
    nav.appendChild(navItem("🛠", "DDL / Schema", { kind: "ddl", index: 0 }));
  }

  function navItem(icon, label, target) {
    var b = el("button", "nav-item");
    b.type = "button";
    b.appendChild(el("span", "ico", icon));
    b.appendChild(el("span", null, label));
    b.dataset.key = target.kind + ":" + target.index;
    b.addEventListener("click", function () { go(target); });
    return b;
  }

  function markActive() {
    var key = route.kind + ":" + (route.kind === "entity" ? route.index : 0);
    Array.prototype.forEach.call($("nav").children, function (n) {
      if (n.classList && n.classList.contains("nav-item"))
        n.classList.toggle("active", n.dataset.key === key);
    });
  }

  function go(target) {
    route = target;
    markActive();
    clear($("page-actions"));
    if (target.kind === "dashboard") renderDashboard();
    else if (target.kind === "entity") renderEntity(target.index);
    else if (target.kind === "reports") renderReports();
    else if (target.kind === "ddl") renderDdl();
  }

  function setTitle(title, sub) {
    $("page-title").textContent = title;
    $("page-sub").textContent = sub || "";
  }

  function loading(node) {
    clear(node).appendChild(el("div", "spinner-row", "Running query…"));
  }

  function loadMeta() {
    return apiGet("/api/meta").then(function (data) {
      meta = data;
      buildNav();
    });
  }

  /* ------------------------------------------------------------------ *
   * Result table rendering
   * ------------------------------------------------------------------ */

  /* Renders {columns, rows} into a scrollable table. actions(row, idx) may
   * return an array of buttons appended as a trailing cell. */
  function renderTable(data, options) {
    options = options || {};
    var wrap = el("div", "table-wrap");

    if (!data.rows.length) {
      var empty = el("div", "empty-state");
      empty.appendChild(el("div", "big", "◻"));
      empty.appendChild(el("div", null, options.emptyText || "No rows returned."));
      return empty;
    }

    var numericCol = data.columns.map(function (_, c) {
      return data.rows.every(function (r) { return r[c] === null || isNumeric(r[c]); });
    });

    var table = el("table");
    var thead = el("thead");
    var hrow = el("tr");
    data.columns.forEach(function (c) { hrow.appendChild(el("th", null, c)); });
    if (options.actions) hrow.appendChild(el("th", null, ""));
    thead.appendChild(hrow);
    table.appendChild(thead);

    var tbody = el("tbody");
    data.rows.forEach(function (row, idx) {
      var tr = el("tr");
      row.forEach(function (v, c) {
        var td;
        if (v === null) {
          td = el("td", "null", "NULL");
        } else if (numericCol[c]) {
          td = el("td", "num", fmtNumber(v));
        } else {
          td = el("td", null, v);
        }
        tr.appendChild(td);
      });
      if (options.actions) {
        var cell = el("td", "row-actions");
        options.actions(row, idx).forEach(function (b) { cell.appendChild(b); });
        tr.appendChild(cell);
      }
      tbody.appendChild(tr);
    });
    table.appendChild(tbody);
    wrap.appendChild(table);
    return wrap;
  }

  function errorBox(message) {
    return el("pre", "err-box", message);
  }

  /* ------------------------------------------------------------------ *
   * Dashboard
   * ------------------------------------------------------------------ */

  var DASH_TILES = [
    { key: "MINE_SITES",         label: "Mine sites",         accent: true },
    { key: "ACTIVE_MINES",       label: "Active mines" },
    { key: "MINERALS",           label: "Minerals" },
    { key: "MINERS",             label: "Miners" },
    { key: "EQUIPMENT",          label: "Equipment units" },
    { key: "PRODUCTION_RECORDS", label: "Production records" },
    { key: "SAFETY_INSPECTIONS", label: "Safety inspections" },
    { key: "TOTAL_TONNES",       label: "Total tonnes produced", accent: true },
    { key: "EQUIPMENT_VALUE",    label: "Equipment purchase value" }
  ];

  function renderDashboard() {
    setTitle("Dashboard", "Live totals read straight from the Oracle schema.");
    var page = clear($("page"));

    var refresh = el("button", "btn sm", "Refresh");
    refresh.type = "button";
    refresh.addEventListener("click", renderDashboard);
    clear($("page-actions")).appendChild(refresh);

    var stats = el("div", "stats");
    page.appendChild(stats);
    DASH_TILES.forEach(function (t) {
      var card = el("div", "stat" + (t.accent ? " accent" : ""));
      card.appendChild(el("div", "k", t.label));
      card.appendChild(el("div", "v", "…"));
      card.dataset.key = t.key;
      stats.appendChild(card);
    });

    var card = el("div", "card");
    var head = el("div", "card-head");
    var headText = el("div");
    headText.appendChild(el("h3", null, "Production by mine and mineral"));
    headText.appendChild(el("p", null, "JOIN across mine sites, production records and minerals."));
    head.appendChild(headText);
    card.appendChild(head);
    var body = el("div", "card-body");
    card.appendChild(body);
    page.appendChild(el("div", "section-title", "Highlights"));
    page.appendChild(card);
    loading(body);

    apiGet("/api/dashboard").then(function (data) {
      var row = data.rows[0] || [];
      Array.prototype.forEach.call(stats.children, function (node) {
        var c = data.columns.indexOf(node.dataset.key);
        var v = c >= 0 ? row[c] : null;
        node.querySelector(".v").textContent = v === null || v === undefined ? "0" : fmtNumber(v);
      });
    }).catch(function (err) {
      Array.prototype.forEach.call(stats.children, function (node) {
        node.querySelector(".v").textContent = "—";
      });
      handleError(err);
    });

    apiGet("/api/report", { report: 0 }).then(function (data) {
      clear(body).appendChild(renderTable(data, {
        emptyText: "No production records yet. Add some under Production Records."
      }));
    }).catch(function (err) {
      clear(body).appendChild(errorBox(err.message));
    });
  }

  /* ------------------------------------------------------------------ *
   * Entity CRUD
   * ------------------------------------------------------------------ */

  function renderEntity(index) {
    var ent = meta.entities[index];
    setTitle(ent.title, "Table " + ent.table + " — insert, view, search, update and delete.");

    var addBtn = el("button", "btn primary", "+ New record");
    addBtn.type = "button";
    clear($("page-actions")).appendChild(addBtn);

    var page = clear($("page"));
    var card = el("div", "card");

    var toolbar = el("div", "toolbar");
    var search = document.createElement("input");
    search.type = "text";
    search.placeholder = "Search " + ent.searchColumn + "…";
    search.autocomplete = "off";
    var searchBtn = el("button", "btn sm", "Search");
    searchBtn.type = "button";
    var clearBtn = el("button", "btn sm ghost", "Show all");
    clearBtn.type = "button";
    var count = el("span", "count", "");
    toolbar.appendChild(search);
    toolbar.appendChild(searchBtn);
    toolbar.appendChild(clearBtn);
    toolbar.appendChild(el("span", "spacer"));
    toolbar.appendChild(count);
    card.appendChild(toolbar);

    var body = el("div");
    card.appendChild(body);
    page.appendChild(card);

    function load(term) {
      loading(body);
      var p = term
        ? apiGet("/api/search", { entity: index, q: term })
        : apiGet("/api/list", { entity: index });

      p.then(function (data) {
        count.textContent = data.rowCount + (data.rowCount === 1 ? " row" : " rows")
                          + (term ? ' matching "' + term + '"' : "");
        clear(body).appendChild(renderTable(data, {
          emptyText: term ? "Nothing matched that search." : "This table is empty.",
          actions: function (row) {
            var edit = el("button", "btn sm", "Edit");
            edit.type = "button";
            edit.addEventListener("click", function () {
              openUpdate(ent, index, data.columns, row, reload);
            });
            var del = el("button", "btn sm danger", "Delete");
            del.type = "button";
            del.addEventListener("click", function () {
              openDelete(ent, index, row, reload);
            });
            return [edit, del];
          }
        }));
      }).catch(function (err) {
        count.textContent = "";
        clear(body).appendChild(errorBox(err.message));
        if (err.status === 401) handleError(err);
      });
    }

    function reload() { load(search.value.trim()); }

    searchBtn.addEventListener("click", reload);
    clearBtn.addEventListener("click", function () { search.value = ""; load(""); });
    search.addEventListener("keydown", function (e) {
      if (e.key === "Enter") { e.preventDefault(); reload(); }
    });

    addBtn.addEventListener("click", function () { openInsert(ent, index, reload); });
    load("");
  }

  function openInsert(ent, index, onDone) {
    openModal("New " + ent.title.replace(/s$/, "") + " record", function (body) {
      body.appendChild(el("p", "note",
        "Values are sent as bound parameters. Leave an optional field empty to store NULL."));
      ent.fields.forEach(function (f) { fieldInput(body, f, "", false); });
    }, function () {
      var inputs = $("modal-body").querySelectorAll("input");
      var payload = { entity: index };
      var pk = "";
      Array.prototype.forEach.call(inputs, function (input, i) {
        payload["v" + i] = input.value.trim();
        if (i === 0) pk = input.value.trim();
      });
      if (!pk) return Promise.reject(new Error(ent.fields[0].label + " is required."));

      return apiPost("/api/insert", payload).then(function (res) {
        toast(res.message + " (" + res.rowsAffected + " row)", "ok");
        onDone();
      });
    }, "Insert record");
  }

  function openUpdate(ent, index, columns, row, onDone) {
    var byColumn = {};
    columns.forEach(function (c, i) { byColumn[c] = row[i]; });

    openModal("Edit " + ent.fields[0].label + " " + byColumn[ent.fields[0].column],
      function (body) {
        body.appendChild(el("p", "note",
          "The primary key identifies the row; every other column is rewritten. "
          + "Clear a field to store NULL."));
        ent.fields.forEach(function (f, i) {
          fieldInput(body, f, byColumn[f.column], i === 0);
        });
      },
      function () {
        var inputs = $("modal-body").querySelectorAll("input");
        var payload = { entity: index, id: inputs[0].value.trim() };
        Array.prototype.forEach.call(inputs, function (input, i) {
          if (i > 0) payload["v" + i] = input.value.trim();
        });
        return apiPost("/api/update", payload).then(function (res) {
          toast(res.message + " (" + res.rowsAffected + " row)",
                res.rowsAffected > 0 ? "ok" : "");
          onDone();
        });
      }, "Save changes");
  }

  function openDelete(ent, index, row, onDone) {
    var id = row[0];
    openModal("Delete record " + id, function (body) {
      body.appendChild(el("p", "note",
        "This permanently deletes " + ent.fields[0].label + " " + id + " from "
        + ent.table + ". It cannot be undone."));
      var pre = el("pre", "sql-box",
        "DELETE FROM " + ent.table + " WHERE " + ent.fields[0].column + " = " + id);
      body.appendChild(pre);
      body.appendChild(el("p", "note",
        "Rows in other tables that reference this record will block the delete."));
    }, function () {
      return apiPost("/api/delete", { entity: index, id: id, confirm: "YES" })
        .then(function (res) {
          toast(res.message + " (" + res.rowsAffected + " row)",
                res.rowsAffected > 0 ? "ok" : "");
          onDone();
        });
    }, "Delete permanently", "danger");
  }

  /* ------------------------------------------------------------------ *
   * Reports
   * ------------------------------------------------------------------ */

  function renderReports() {
    setTitle("Reports / DQL queries",
             "Read-only SELECT queries using JOIN, GROUP BY, SUM, COUNT and ORDER BY.");
    var page = clear($("page"));

    meta.reports.forEach(function (rep) {
      var card = el("div", "card");
      var head = el("div", "card-head");
      var text = el("div");
      text.appendChild(el("h3", null, rep.title));
      text.appendChild(el("p", null, rep.description));
      head.appendChild(text);

      var run = el("button", "btn sm", "Run query");
      run.type = "button";
      head.appendChild(run);
      card.appendChild(head);

      var body = el("div", "card-body");
      body.appendChild(el("div", "spinner-row", "Not run yet."));
      card.appendChild(body);
      page.appendChild(card);

      function execute() {
        run.disabled = true;
        loading(body);
        apiGet("/api/report", { report: rep.index }).then(function (data) {
          clear(body).appendChild(renderTable(data));
          var foot = el("div", "count", data.rowCount
            + (data.rowCount === 1 ? " row" : " rows") + " returned");
          foot.style.padding = "11px 0 0";
          body.appendChild(foot);
        }).catch(function (err) {
          clear(body).appendChild(errorBox(err.message));
          if (err.status === 401) handleError(err);
        }).then(function () { run.disabled = false; });
      }

      run.addEventListener("click", execute);
      if (rep.index === 0) execute();
    });
  }

  /* ------------------------------------------------------------------ *
   * DDL
   * ------------------------------------------------------------------ */

  function renderDdl() {
    setTitle("DDL / Schema management",
             "CREATE, ALTER and DROP statements plus a read of USER_TAB_COLUMNS.");
    var page = clear($("page"));

    page.appendChild(el("div", "section-title", "Schema operations"));

    meta.ddl.forEach(function (op) {
      var card = el("div", "card");
      var head = el("div", "card-head");
      var text = el("div");
      var h = el("h3", null, op.title);
      if (op.destructive) {
        h.appendChild(document.createTextNode(" "));
        h.appendChild(el("span", "badge bad", "destructive"));
      }
      text.appendChild(h);
      text.appendChild(el("p", null, op.description));
      head.appendChild(text);

      var run = el("button", "btn sm" + (op.destructive ? " danger" : ""),
                   op.isQuery ? "Run query" : "Execute");
      run.type = "button";
      head.appendChild(run);
      card.appendChild(head);

      var body = el("div", "card-body");
      body.appendChild(el("pre", "sql-box", op.sql));
      card.appendChild(body);
      page.appendChild(card);

      function execute(confirmValue) {
        run.disabled = true;
        var out = el("div");
        loading(out);
        clear(body);
        body.appendChild(el("pre", "sql-box", op.sql));
        body.appendChild(out);

        var params = { op: op.index };
        if (confirmValue) params.confirm = confirmValue;

        apiPost("/api/ddl", params).then(function (data) {
          clear(out);
          if (data.rows) {
            out.appendChild(renderTable(data));
          } else {
            out.appendChild(el("p", "count", data.message));
            toast(data.message, "ok");
          }
        }).catch(function (err) {
          clear(out).appendChild(errorBox(err.message));
          if (err.status === 401) handleError(err);
        }).then(function () { run.disabled = false; });
      }

      run.addEventListener("click", function () {
        if (!op.destructive) { execute(null); return; }

        openModal(op.title, function (mbody) {
          mbody.appendChild(el("p", "note",
            "This drops the table and everything in it. Type DROP to confirm."));
          mbody.appendChild(el("pre", "sql-box", op.sql));
          var wrap = el("div", "field");
          var label = el("label", null, "Confirmation");
          label.setAttribute("for", "ddl-confirm");
          wrap.appendChild(label);
          var input = document.createElement("input");
          input.id = "ddl-confirm";
          input.type = "text";
          input.placeholder = "DROP";
          input.autocomplete = "off";
          wrap.appendChild(input);
          mbody.appendChild(wrap);
        }, function () {
          var v = $("ddl-confirm").value.trim();
          if (v !== "DROP") return Promise.reject(new Error('You must type DROP exactly.'));
          execute("DROP");
          return Promise.resolve();
        }, "Drop the table", "danger");
      });
    });

    page.appendChild(el("div", "section-title", "Note"));
    var note = el("div", "card");
    var nb = el("div", "card-body");
    nb.appendChild(el("p", "note",
      "Repeating CREATE TABLE or ALTER TABLE for an object that already exists returns "
      + "an Oracle error. That is expected — the error text is shown as the driver "
      + "reported it. Oracle commits DDL implicitly, so these changes cannot be rolled back."));
    note.appendChild(nb);
    page.appendChild(note);
  }

  /* ------------------------------------------------------------------ *
   * Start
   * ------------------------------------------------------------------ */

  apiGet("/api/status").then(function (data) {
    if (!data.connected) { showLogin(null); return; }
    showApp(data.user, data.dsn);
    return loadMeta().then(function () { go({ kind: "dashboard", index: 0 }); });
  }).catch(function () {
    showLogin("Could not reach the server. Is miningweb.exe still running?");
  });

})();
