const express = require('express');
const fs = require('fs');
const path = require('path');

const app = express();
const PORT = process.env.PORT || 3000;
const configPath = path.join(__dirname, 'mc_reference.json');

app.use(express.json());
app.use(express.static(__dirname));

app.get('/config', (req, res) => {
  fs.readFile(configPath, 'utf8', (err, data) => {
    if (err) {
      return res.status(500).json({ error: 'read error' });
    }
    res.type('json').send(data);
  });
});

app.post('/config', (req, res) => {
  fs.writeFile(configPath, JSON.stringify(req.body, null, 2), (err) => {
    if (err) {
      return res.status(500).json({ error: 'write error' });
    }
    res.json({ status: 'ok' });
  });
});

app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});
