(() => {
  const queryInput = document.getElementById('destinationQuery');
  const searchButton = document.getElementById('destinationSearchButton');
  const resultsContainer = document.getElementById('destinationResults');
  const status = document.getElementById('destinationSearchStatus');
  const latitudeInput = document.getElementById('targetLat');
  const longitudeInput = document.getElementById('targetLng');

  if (!queryInput || !searchButton || !resultsContainer || !status || !latitudeInput || !longitudeInput) {
    return;
  }

  let lastRequestAt = 0;

  async function searchDestination() {
    const query = queryInput.value.trim();
    if (!query) {
      status.textContent = '住所または施設名を入力してください。';
      resultsContainer.replaceChildren();
      return;
    }

    searchButton.disabled = true;
    resultsContainer.replaceChildren();
    status.textContent = '住所を検索しています…';

    try {
      const delay = Math.max(0, 1000 - (Date.now() - lastRequestAt));
      if (delay > 0) {
        await new Promise(resolve => setTimeout(resolve, delay));
      }

      lastRequestAt = Date.now();
      const url = new URL('https://nominatim.openstreetmap.org/search');
      url.search = new URLSearchParams({
        q: query,
        format: 'jsonv2',
        countrycodes: 'jp',
        limit: '5',
        'accept-language': 'ja'
      });

      const response = await fetch(url);
      if (!response.ok) {
        throw new Error(`検索サービスの応答: ${response.status}`);
      }

      const results = (await response.json()).filter(result => {
        const latitude = Number(result.lat);
        const longitude = Number(result.lon);
        return Number.isFinite(latitude) && latitude >= -90 && latitude <= 90 &&
          Number.isFinite(longitude) && longitude >= -180 && longitude <= 180;
      });

      if (results.length === 0) {
        status.textContent = '候補が見つかりません。住所や施設名を変えて再検索してください。';
        return;
      }

      status.textContent = '候補を選択してください。';
      for (const result of results) {
        const latitude = Number(result.lat);
        const longitude = Number(result.lon);
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'destination-search__result';
        button.textContent = result.display_name;
        button.addEventListener('click', () => {
          latitudeInput.value = latitude.toFixed(6);
          longitudeInput.value = longitude.toFixed(6);
          document.getElementById('btnSetTarget')?.click();
          status.textContent = `目的地を設定しました: ${result.display_name}`;
          resultsContainer.replaceChildren();
        });
        resultsContainer.appendChild(button);
      }
    } catch (error) {
      status.textContent = `住所を検索できませんでした。通信状態を確認してください。(${error.message})`;
    } finally {
      searchButton.disabled = false;
    }
  }

  searchButton.addEventListener('click', searchDestination);
  queryInput.addEventListener('keydown', event => {
    if (event.key === 'Enter') {
      event.preventDefault();
      searchDestination();
    }
  });
})();