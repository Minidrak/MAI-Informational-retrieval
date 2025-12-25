#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Поисковый робот для обкачки документов из указанных источников.
Сохраняет документы в MongoDB с возможностью продолжения работы после остановки.
"""

import sys
import os
import time
import hashlib
import yaml
import urllib.parse
import urllib.robotparser
from datetime import datetime, timedelta
from typing import List, Dict, Set, Optional
import re

try:
    import requests
    from bs4 import BeautifulSoup
    from pymongo import MongoClient
    from pymongo.errors import ConnectionFailure, DuplicateKeyError
except ImportError as e:
    print(f"Ошибка импорта: {e}")
    print("Установите необходимые зависимости: pip install -r requirements.txt")
    sys.exit(1)


class URLCrawler:
    """Класс для нормализации и обработки URL."""
    
    @staticmethod
    def normalize_url(url: str, base_url: str = None) -> str:
        """
        Нормализует URL: удаляет фрагменты, сортирует параметры и т.д.
        
        Args:
            url: URL для нормализации
            base_url: Базовый URL для разрешения относительных ссылок
            
        Returns:
            Нормализованный URL
        """
        if not url:
            return ""
        
        # Разрешаем относительные URL
        if base_url:
            url = urllib.parse.urljoin(base_url, url)
        
        # Парсим URL
        parsed = urllib.parse.urlparse(url)
        
        # Удаляем фрагмент (#)
        parsed = parsed._replace(fragment='')
        
        # Нормализуем путь (убираем лишние слеши)
        path = urllib.parse.unquote(parsed.path)
        path = re.sub(r'/+', '/', path)
        if path and path != '/' and path.endswith('/'):
            path = path[:-1]
        
        # Сортируем параметры запроса для единообразия
        query_params = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)
        sorted_params = sorted(query_params.items())
        query = urllib.parse.urlencode(sorted_params, doseq=True)
        
        # Собираем URL обратно
        normalized = urllib.parse.urlunparse((
            parsed.scheme.lower(),
            parsed.netloc.lower(),
            path,
            parsed.params,
            query,
            ''  # fragment уже удален
        ))
        
        return normalized
    
    @staticmethod
    def is_valid_url(url: str) -> bool:
        """Проверяет, является ли URL валидным."""
        try:
            parsed = urllib.parse.urlparse(url)
            return bool(parsed.scheme and parsed.netloc)
        except:
            return False
    
    @staticmethod
    def get_domain(url: str) -> str:
        """Извлекает домен из URL."""
        try:
            parsed = urllib.parse.urlparse(url)
            return parsed.netloc.lower()
        except:
            return ""


class DocumentCrawler:
    """Основной класс поискового робота."""
    
    def __init__(self, config_path: str):
        """
        Инициализирует поисковый робот.
        
        Args:
            config_path: Путь к YAML конфигурационному файлу
        """
        self.config = self._load_config(config_path)
        self.db_client = None
        self.db_collection = None
        self.visited_urls: Set[str] = set()
        self.url_queue: List[Dict] = []
        self.session = requests.Session()
        self.failed_urls: Set[str] = set()  # URL, которые не удалось загрузить
        self.robots_parsers: Dict[str, urllib.robotparser.RobotFileParser] = {}
        self.crawl_delays: Dict[str, float] = {}  # Задержки для каждого домена
        
    def _load_config(self, config_path: str) -> Dict:
        """Загружает конфигурацию из YAML файла."""
        try:
            with open(config_path, 'r', encoding='utf-8') as f:
                config = yaml.safe_load(f)
            return config
        except FileNotFoundError:
            print(f"Ошибка: файл конфигурации '{config_path}' не найден")
            sys.exit(1)
        except yaml.YAMLError as e:
            print(f"Ошибка парсинга YAML: {e}")
            sys.exit(1)
    
    def _connect_db(self):
        """Подключается к MongoDB."""
        db_config = self.config['db']
        try:
            # Формируем строку подключения
            if db_config.get('username') and db_config.get('password'):
                connection_string = (
                    f"mongodb://{db_config['username']}:{db_config['password']}"
                    f"@{db_config['host']}:{db_config['port']}/"
                )
            else:
                connection_string = f"mongodb://{db_config['host']}:{db_config['port']}/"
            
            self.db_client = MongoClient(connection_string, serverSelectionTimeoutMS=5000)
            # Проверяем подключение
            self.db_client.server_info()
            
            db = self.db_client[db_config['database']]
            self.db_collection = db[db_config['collection']]
            
            # Создаем индекс для быстрого поиска по URL
            self.db_collection.create_index("url", unique=True)
            self.db_collection.create_index("crawl_date")
            
            print(f"Подключение к MongoDB успешно: {db_config['host']}:{db_config['port']}")
        except ConnectionFailure:
            print("Ошибка: не удалось подключиться к MongoDB")
            print("Убедитесь, что MongoDB запущен и доступен")
            sys.exit(1)
        except Exception as e:
            print(f"Ошибка подключения к базе данных: {e}")
            sys.exit(1)
    
    def _load_visited_urls(self):
        """Загружает список уже обработанных URL из базы данных."""
        try:
            cursor = self.db_collection.find({}, {"url": 1})
            self.visited_urls = {doc["url"] for doc in cursor}
            print(f"Загружено {len(self.visited_urls)} уже обработанных URL")
        except Exception as e:
            print(f"Предупреждение: не удалось загрузить список обработанных URL: {e}")
            self.visited_urls = set()
    
    def _should_recheck(self, url: str) -> bool:
        """
        Определяет, нужно ли переобкачивать документ.
        
        Args:
            url: URL документа
            
        Returns:
            True, если документ нужно переобкачать
        """
        try:
            doc = self.db_collection.find_one({"url": url})
            if not doc:
                return True
            
            crawl_date = doc.get("crawl_date", 0)
            recheck_interval = self.config['logic'].get('recheck_interval_days', 7)
            recheck_seconds = recheck_interval * 24 * 60 * 60
            
            return (time.time() - crawl_date) > recheck_seconds
        except Exception as e:
            print(f"Ошибка при проверке необходимости переобкачки: {e}")
            return True
    
    def _get_user_agent(self) -> str:
        """Возвращает User-Agent для запросов."""
        return self.config['logic'].get('user_agent', 
            'MAI-Crawler/1.0 (Educational Research Bot; +https://github.com/your-repo)')
    
    def _get_headers(self) -> Dict[str, str]:
        """Возвращает заголовки для HTTP запросов."""
        user_agent = self._get_user_agent()
        return {
            'User-Agent': user_agent,
            'Accept': 'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
            'Accept-Language': 'ru-RU,ru;q=0.9,en-US;q=0.8,en;q=0.7',
            'Accept-Encoding': 'gzip, deflate',
            'Connection': 'keep-alive'
        }
    
    def _get_robots_parser(self, url: str) -> Optional[urllib.robotparser.RobotFileParser]:
        """
        Получает или создает парсер robots.txt для домена.
        
        Args:
            url: URL для определения домена
            
        Returns:
            Парсер robots.txt или None
        """
        domain = URLCrawler.get_domain(url)
        if not domain:
            return None
        
        if domain not in self.robots_parsers:
            try:
                parsed_url = urllib.parse.urlparse(url)
                robots_url = f"{parsed_url.scheme}://{domain}/robots.txt"
                print(f"[robots.txt] Загрузка robots.txt для {domain}: {robots_url}")
                rp = urllib.robotparser.RobotFileParser()
                rp.set_url(robots_url)
                rp.read()
                self.robots_parsers[domain] = rp
                print(f"[robots.txt] robots.txt успешно загружен для {domain}")
                
                # Извлекаем crawl-delay
                MIN_DELAY = 5.0  # Минимальная задержка в секундах
                user_agent = self._get_user_agent()
                try:
                    # Пробуем получить crawl-delay для нашего User-Agent
                    crawl_delay = rp.crawl_delay(user_agent)
                    if crawl_delay is not None:
                        # Гарантируем минимум 5 секунд
                        delay_value = max(float(crawl_delay), MIN_DELAY)
                        self.crawl_delays[domain] = delay_value
                        print(f"[robots.txt] Для домена {domain} установлен crawl-delay: {delay_value} сек (минимум {MIN_DELAY} сек)")
                    else:
                        print(f"[robots.txt] Crawl-delay не указан для {domain}, используется значение по умолчанию: {MIN_DELAY} сек")
                except Exception as e:
                    # Если crawl-delay не указан, используем значение по умолчанию
                    print(f"[robots.txt] Не удалось получить crawl-delay для {domain}: {e}")
                    
            except Exception as e:
                # Если не удалось прочитать robots.txt, разрешаем обкачку
                print(f"[robots.txt] Не удалось загрузить robots.txt для {domain}: {e}")
                print(f"[robots.txt] Продолжаем обкачку без проверки robots.txt")
                self.robots_parsers[domain] = None
        
        return self.robots_parsers.get(domain)
    
    def _can_fetch(self, url: str) -> bool:
        """
        Проверяет, разрешена ли обкачка URL согласно robots.txt.
        
        Args:
            url: URL для проверки
            
        Returns:
            True, если обкачка разрешена
        """
        # Если это обычная статья Википедии, разрешаем обкачку
        # Согласно robots.txt Википедии: "Friendly, low-speed bots are welcome viewing article pages"
        if self._is_wikipedia_article_url(url):
            return True
        
        # Проверяем, нужно ли соблюдать robots.txt
        respect_robots = self.config['logic'].get('respect_robots_txt', True)
        if not respect_robots:
            # Но все равно проверяем специфичные запрещенные пути Википедии
            if self._is_wikipedia_disallowed_path(url):
                return False
            return True  # Игнорируем robots.txt
        
        # Дополнительная проверка запрещенных путей для Википедии
        if self._is_wikipedia_disallowed_path(url):
            return False
        
        parser = self._get_robots_parser(url)
        if parser is None:
            return True
        
        user_agent = self._get_user_agent()
        can_fetch = parser.can_fetch(user_agent, url)
        
        if not can_fetch:
            # Детальная информация о том, почему запрещено (только для первого случая)
            domain = URLCrawler.get_domain(url)
            # Проверяем, является ли это служебной страницей Википедии
            is_wiki_service_page = self._is_wikipedia_disallowed_path(url)
            
            if is_wiki_service_page:
                # Для служебных страниц просто пропускаем без детального вывода
                pass
            else:
                # Для обычных страниц выводим информацию только один раз
                if domain not in getattr(self, '_robots_warned_domains', set()):
                    if not hasattr(self, '_robots_warned_domains'):
                        self._robots_warned_domains = set()
                    self._robots_warned_domains.add(domain)
                    print(f"  [robots.txt] Обкачка некоторых страниц запрещена для домена {domain}")
                    print(f"  [robots.txt] Запрещенные страницы будут пропущены, обкачка продолжается")
        
        return can_fetch
    
    def _get_crawl_delay(self, url: str) -> float:
        """
        Получает задержку между запросами для домена из robots.txt.
        Гарантирует минимум 5 секунд для предотвращения блокировок.
        
        Args:
            url: URL для определения домена
            
        Returns:
            Задержка в секундах (минимум 5.0)
        """
        MIN_DELAY = 5.0  # Минимальная задержка в секундах
        
        domain = URLCrawler.get_domain(url)
        if domain in self.crawl_delays:
            delay = self.crawl_delays[domain]
            return max(delay, MIN_DELAY)  # Гарантируем минимум
        
        # Возвращаем значение по умолчанию из конфига, но не меньше минимума
        default_delay = self.config['logic'].get('delay_between_requests', MIN_DELAY)
        return max(default_delay, MIN_DELAY)
    
    def _fetch_page(self, url: str, retry_count: int = 3) -> Optional[requests.Response]:
        """
        Загружает страницу по URL с повторными попытками.
        
        Args:
            url: URL страницы
            retry_count: Количество попыток при ошибке
            
        Returns:
            Response объект или None при ошибке
        """
        # Проверяем robots.txt перед загрузкой
        if not self._can_fetch(url):
            # Проверяем, является ли это запрещенной страницей Википедии
            domain = URLCrawler.get_domain(url)
            is_wiki_disallowed = self._is_wikipedia_disallowed_path(url)
            
            # Выводим сообщение только для неслужебных страниц и не слишком часто
            if not is_wiki_disallowed:
                # Считаем количество пропущенных страниц для этого домена
                if not hasattr(self, '_skipped_count'):
                    self._skipped_count = {}
                if domain not in self._skipped_count:
                    self._skipped_count[domain] = 0
                self._skipped_count[domain] += 1
                
                # Выводим сообщение только каждые 10 пропущенных страниц
                if self._skipped_count[domain] % 10 == 1:
                    print(f"[ПРОПУСК] Пропущено {self._skipped_count[domain]} страниц для {domain} (robots.txt). Продолжаем обкачку...")
            
            self.failed_urls.add(url)
            return None  # Пропускаем эту страницу, но продолжаем работу
        
        # Обновляем заголовки для каждого запроса
        self.session.headers.update(self._get_headers())
        timeout = self.config['logic'].get('request_timeout', 30)
        
        for attempt in range(retry_count):
            try:
                # Правильно кодируем URL, если он содержит кириллицу
                try:
                    # Проверяем, нужно ли кодировать URL
                    parsed = urllib.parse.urlparse(url)
                    if any(ord(c) > 127 for c in parsed.path):
                        # URL содержит не-ASCII символы, кодируем их
                        encoded_path = urllib.parse.quote(parsed.path, safe='/')
                        url = urllib.parse.urlunparse((
                            parsed.scheme,
                            parsed.netloc,
                            encoded_path,
                            parsed.params,
                            parsed.query,
                            parsed.fragment
                        ))
                except:
                    pass
                
                response = self.session.get(
                    url, 
                    timeout=timeout, 
                    allow_redirects=True,
                    stream=False
                )
                
                # Проверяем статус код
                if response.status_code == 200:
                    response.encoding = response.apparent_encoding or 'utf-8'
                    self.failed_urls.discard(url)  # Убираем из списка неудачных, если успешно
                    return response
                elif response.status_code == 429:
                    # Too Many Requests - ждем дольше
                    wait_time = (attempt + 1) * 5
                    print(f"Получен код 429 (Too Many Requests) для {url}. Ожидание {wait_time} сек...")
                    time.sleep(wait_time)
                    continue
                elif response.status_code in [403, 404]:
                    # Доступ запрещен или страница не найдена
                    print(f"Код {response.status_code} для {url}: {response.reason}")
                    self.failed_urls.add(url)
                    return None
                else:
                    response.raise_for_status()
                    
            except requests.exceptions.Timeout:
                print(f"Таймаут при загрузке {url} (попытка {attempt + 1}/{retry_count})")
                if attempt < retry_count - 1:
                    MIN_DELAY = 5.0
                    wait_time = max(2 * (attempt + 1), MIN_DELAY)  # Минимум 5 секунд
                    time.sleep(wait_time)
                    continue
                else:
                    self.failed_urls.add(url)
                    return None
                    
            except requests.exceptions.ConnectionError as e:
                print(f"Ошибка соединения при загрузке {url} (попытка {attempt + 1}/{retry_count}): {e}")
                if attempt < retry_count - 1:
                    MIN_DELAY = 5.0
                    wait_time = max(3 * (attempt + 1), MIN_DELAY)  # Минимум 5 секунд
                    time.sleep(wait_time)
                    continue
                else:
                    self.failed_urls.add(url)
                    return None
                    
            except requests.exceptions.HTTPError as e:
                print(f"HTTP ошибка при загрузке {url} (попытка {attempt + 1}/{retry_count}): {e}")
                if attempt < retry_count - 1 and response.status_code >= 500:
                    # Повторяем только для серверных ошибок
                    MIN_DELAY = 5.0
                    wait_time = max(2 * (attempt + 1), MIN_DELAY)  # Минимум 5 секунд
                    time.sleep(wait_time)
                    continue
                else:
                    self.failed_urls.add(url)
                    return None
                    
            except requests.exceptions.RequestException as e:
                print(f"Ошибка при загрузке {url} (попытка {attempt + 1}/{retry_count}): {e}")
                if attempt < retry_count - 1:
                    MIN_DELAY = 5.0
                    wait_time = max(2 * (attempt + 1), MIN_DELAY)  # Минимум 5 секунд
                    time.sleep(wait_time)
                    continue
                else:
                    self.failed_urls.add(url)
                    return None
            except Exception as e:
                print(f"Неожиданная ошибка при загрузке {url}: {e}")
                self.failed_urls.add(url)
                return None
        
        print(f"Не удалось загрузить {url} после {retry_count} попыток")
        self.failed_urls.add(url)
        return None
    
    def _extract_links(self, html: str, base_url: str) -> List[str]:
        """
        Извлекает ссылки из HTML.
        
        Args:
            html: HTML содержимое страницы
            base_url: Базовый URL для разрешения относительных ссылок
            
        Returns:
            Список нормализованных URL
        """
        links = []
        try:
            soup = BeautifulSoup(html, 'html.parser')
            for tag in soup.find_all('a', href=True):
                href = tag['href']
                normalized = URLCrawler.normalize_url(href, base_url)
                
                if not normalized or not URLCrawler.is_valid_url(normalized):
                    continue
                
                # Исключаем файлы
                if self._is_file_url(normalized):
                    continue
                
                # Для Википедии проверяем, что это обычная статья (не категория, не служебная страница)
                domain = URLCrawler.get_domain(normalized)
                if 'wikipedia.org' in domain:
                    if not self._is_wikipedia_article_url(normalized):
                        continue  # Пропускаем категории, служебные страницы и файлы
                
                links.append(normalized)
        except Exception as e:
            print(f"Ошибка при извлечении ссылок: {e}")
        
        return links
    
    def _calculate_content_hash(self, html: str) -> str:
        """
        Вычисляет хеш содержимого документа для проверки изменений.
        
        Args:
            html: HTML содержимое
            
        Returns:
            SHA256 хеш содержимого
        """
        return hashlib.sha256(html.encode('utf-8')).hexdigest()
    
    def _save_document(self, url: str, html: str, source_name: str) -> bool:
        """
        Сохраняет документ в базу данных.
        
        Args:
            url: Нормализованный URL
            html: HTML содержимое документа
            source_name: Название источника
            
        Returns:
            True, если документ был сохранен или обновлен
        """
        try:
            content_hash = self._calculate_content_hash(html)
            crawl_date = int(time.time())
            
            # Проверяем, существует ли документ
            existing_doc = self.db_collection.find_one({"url": url})
            
            if existing_doc:
                # Если документ не изменился, обновляем только дату обкачки
                if existing_doc.get("content_hash") == content_hash:
                    self.db_collection.update_one(
                        {"url": url},
                        {"$set": {"crawl_date": crawl_date}}
                    )
                    print(f"Документ не изменился, обновлена дата: {url}")
                else:
                    # Документ изменился, обновляем его
                    self.db_collection.update_one(
                        {"url": url},
                        {
                            "$set": {
                                "html_content": html,
                                "crawl_date": crawl_date,
                                "content_hash": content_hash,
                                "source_name": source_name
                            }
                        }
                    )
                    print(f"Документ обновлен: {url}")
            else:
                # Новый документ
                document = {
                    "url": url,
                    "html_content": html,
                    "source_name": source_name,
                    "crawl_date": crawl_date,
                    "content_hash": content_hash
                }
                self.db_collection.insert_one(document)
                print(f"Документ сохранен: {url}")
            
            return True
        except DuplicateKeyError:
            print(f"Предупреждение: дубликат URL (должен быть обработан): {url}")
            return False
        except Exception as e:
            print(f"Ошибка при сохранении документа {url}: {e}")
            return False
    
    def _is_same_domain(self, url1: str, url2: str) -> bool:
        """Проверяет, принадлежат ли URL одному домену."""
        return URLCrawler.get_domain(url1) == URLCrawler.get_domain(url2)
    
    def _is_file_url(self, url: str) -> bool:
        """
        Проверяет, является ли URL файлом (изображением, медиа и т.д.).
        
        Args:
            url: URL для проверки
            
        Returns:
            True, если это файл
        """
        # Расширения файлов для исключения
        file_extensions = [
            '.png', '.jpg', '.jpeg', '.gif', '.svg', '.webp', '.ico',  # Изображения
            '.pdf', '.doc', '.docx', '.xls', '.xlsx', '.ppt', '.pptx',  # Документы
            '.zip', '.rar', '.7z', '.tar', '.gz',  # Архивы
            '.mp3', '.mp4', '.avi', '.mov', '.wmv', '.flv',  # Видео/Аудио
            '.css', '.js', '.json', '.xml',  # Веб-файлы
            '.ttf', '.woff', '.woff2', '.eot',  # Шрифты
        ]
        
        url_lower = url.lower()
        for ext in file_extensions:
            if ext in url_lower or url_lower.endswith(ext):
                return True
        
        # Проверяем пути к файлам в Википедии
        if '/wiki/File:' in url or '/wiki/Файл:' in url or '/wiki/Media:' in url or '/wiki/Медиа:' in url:
            return True
        
        # Проверяем прямые ссылки на файлы
        if '/File:' in url or '/Файл:' in url:
            return True
        
        return False
    
    def _is_wikipedia_article_url(self, url: str) -> bool:
        """
        Проверяет, является ли URL обычной статьей Википедии (не служебной страницей, не категорией, не файлом).
        Согласно robots.txt Википедии, обычные статьи разрешены для обкачки.
        
        Args:
            url: URL для проверки
            
        Returns:
            True, если это обычная статья
        """
        domain = URLCrawler.get_domain(url)
        if 'wikipedia.org' not in domain:
            return False
        
        # Проверяем, что это не файл
        if self._is_file_url(url):
            return False
        
        # Проверяем, что это не запрещенный путь
        if self._is_wikipedia_disallowed_path(url):
            return False
        
        # Проверяем, что это обычная статья (начинается с /wiki/ и не содержит служебных префиксов)
        try:
            decoded_url = urllib.parse.unquote(url)
        except:
            decoded_url = url
        
        # Разрешаем только обычные статьи
        if '/wiki/' in url:
            # Исключаем служебные страницы и категории
            forbidden_prefixes = [
                '/wiki/Википедия:',
                '/wiki/Wikipedia:',
                '/wiki/Участник:',
                '/wiki/Участница:',
                '/wiki/Обсуждение_участника:',
                '/wiki/Обсуждение_участницы:',
                '/wiki/Special:',
                '/wiki/User:',
                '/wiki/User_talk:',
                '/wiki/File:',
                '/wiki/Файл:',
                '/wiki/Media:',
                '/wiki/Медиа:',
                '/wiki/Category:',
                '/wiki/Категория:',
                '/wiki/Category%3A',
                '/wiki/Категория%3A',
                '/wiki/%D0%9A%D0%B0%D1%82%D0%B5%D0%B3%D0%BE%D1%80%D0%B8%D1%8F:',
                '/wiki/%D0%9A%D0%B0%D1%82%D0%B5%D0%B3%D0%BE%D1%80%D0%B8%D1%8F%3A',
            ]
            
            for prefix in forbidden_prefixes:
                if prefix in decoded_url or prefix in url:
                    return False
            
            # Разрешаем только обычные статьи (не категории, не служебные страницы)
            return True
        
        return False
    
    def _is_wikipedia_disallowed_path(self, url: str) -> bool:
        """
        Проверяет, попадает ли URL под запрещенные пути для Википедии согласно robots.txt.
        Это дополнительная проверка для русской Википедии.
        
        Args:
            url: URL для проверки
            
        Returns:
            True, если путь запрещен
        """
        domain = URLCrawler.get_domain(url)
        if 'wikipedia.org' not in domain:
            return False
        
        # Декодируем URL для проверки
        try:
            decoded_url = urllib.parse.unquote(url)
        except:
            decoded_url = url
        
        # Общие запрещенные пути для всех языковых версий
        disallowed_patterns = [
            '/w/',  # Но Allow: /w/api.php?action=mobileview&, /w/load.php?, /w/rest.php/site/v1/sitemap
            '/api/',  # Но Allow: /api/rest_v1/?doc
            '/trap/',
            '/wiki/Special:',
            '/wiki/Spezial:',
            '/wiki/Spesial:',
            '/wiki/Special%3A',
            '/wiki/Spezial%3A',
            '/wiki/Spesial%3A',
        ]
        
        # Проверяем общие запрещенные пути (с исключениями)
        for pattern in disallowed_patterns:
            if pattern in url or pattern in decoded_url:
                # Проверяем исключения для /w/
                if pattern == '/w/':
                    if '/w/api.php?action=mobileview&' in url or '/w/load.php?' in url or '/w/rest.php/site/v1/sitemap' in url:
                        continue  # Разрешено
                    return True
                # Проверяем исключения для /api/
                elif pattern == '/api/':
                    if '/api/rest_v1/?doc' in url:
                        continue  # Разрешено
                    return True
                return True
        
        # Специфичные запрещенные пути для ru.wikipedia.org
        if 'ru.wikipedia.org' in domain:
            ru_disallowed_patterns = [
                '/wiki/Участник:',
                '/wiki/Участник%3A',
                '/wiki/%D0%A3%D1%87%D0%B0%D1%81%D1%82%D0%BD%D0%B8%D0%BA:',
                '/wiki/%D0%A3%D1%87%D0%B0%D1%81%D1%82%D0%BD%D0%B8%D0%BA%3A',
                '/wiki/Участница:',
                '/wiki/Участница%3A',
                '/wiki/%D0%A3%D1%87%D0%B0%D1%81%D1%82%D0%BD%D0%B8%D1%86%D0%B0:',
                '/wiki/%D0%A3%D1%87%D0%B0%D1%81%D1%82%D0%BD%D0%B8%D1%86%D0%B0%3A',
                '/wiki/Обсуждение_участника:',
                '/wiki/Обсуждение_участника%3A',
                '/wiki/%D0%9E%D0%B1%D1%81%D1%83%D0%B6%D0%B4%D0%B5%D0%BD%D0%B8%D0%B5_%D1%83%D1%87%D0%B0%D1%81%D1%82%D0%BD%D0%B8%D0%BA%D0%B0:',
                '/wiki/%D0%9E%D0%B1%D1%81%D1%83%D0%B6%D0%B4%D0%B5%D0%BD%D0%B8%D0%B5_%D1%83%D1%87%D0%B0%D1%81%D1%82%D0%BD%D0%B8%D0%BA%D0%B0%3A',
                '/wiki/Обсуждение_участницы:',
                '/wiki/Обсуждение_участницы%3A',
                '/wiki/%D0%9E%D0%B1%D1%81%D1%83%D0%B6%D0%B4%D0%B5%D0%BD%D0%B8%D0%B5_%D1%83%D1%87%D0%B0%D1%81%D1%82%D0%BD%D0%B8%D1%86%D1%8B:',
                '/wiki/%D0%9E%D0%B1%D1%81%D1%83%D0%B6%D0%B4%D0%B5%D0%BD%D0%B8%D0%B5_%D1%83%D1%87%D0%B0%D1%81%D1%82%D0%BD%D0%B8%D1%86%D1%8B%3A',
                '/wiki/Википедия:Выборы_арбитров/',
                '/wiki/Википедия%3AВыборы_арбитров%2F',
                '/wiki/Википедия:К_удалению',
                '/wiki/Википедия%3AК_удалению',
                '/wiki/Википедия:К_восстановлению',
                '/wiki/Википедия%3AК_восстановлению',
                '/wiki/Википедия:Архив_запросов_на_удаление/',
                '/wiki/Википедия%3AАрхив_запросов_на_удаление%2F',
                '/wiki/Википедия:Проверка_участников',
                '/wiki/Википедия%3AПроверка_участников',
                '/wiki/Википедия:Запросы_к_администраторам',
                '/wiki/Википедия%3AЗапросы_к_администраторам',
                '/wiki/Википедия:Заявки_на_снятие_флагов',
                '/wiki/Википедия%3AЗаявки_на_снятие_флагов',
                '/wiki/Википедия:Запросы,_связанные_с_VRTS',
                '/wiki/Википедия%3AЗапросы%2C_связанные_с_VRTS',
                # Также проверяем варианты с URL-кодированием
                '/wiki/%D0%92%D0%B8%D0%BA%D0%B8%D0%BF%D0%B5%D0%B4%D0%B8%D1%8F:',
                '/wiki/Википедия:Сообщить_об_ошибке',
                '/wiki/Википедия:Как_править_статьи',
                '/wiki/Википедия:Сообщество',
                '/wiki/Википедия:Форум',
                '/wiki/Википедия:Справка',
                '/wiki/Служебная:RecentChanges',  # Свежие правки
                '/wiki/Специальная:NewPages',  # Новые страницы
                '/wiki/Служебная:SpecialPages',  # Служебные страницы
                '/wiki/Служебная:WhatLinksHere',  # Ссылки сюда
                '/wiki/Служебная:RecentChangesLinked',  # Связанные правки
                '/wiki/Служебная:PermanentLink',  # Постоянная ссылка
                '/wiki/Служебная:Info',  # Сведения о странице
                '/wiki/Служебная:ShortUrl',  # Получить короткий URL
                '/wiki/Специальная:QrCode',  # Скачать QR-код
                '/wiki/Служебная:Print',  # Печать/экспорт
                '/wiki/Специальная:DownloadAsPdf',  # Скачать как PDF
                '/wiki/Специальная:PrintableVersion',  # Версия для печати
                

                '/wiki/Викиновости:',
                '/wiki/Викицитатник:',
                '/wiki/%D0%92%D0%B8%D0%BA%D0%B8%D1%86%D0%B8%D1%82%D0%B0%D1%82%D0%BD%D0%B8%D0%BA:',
                '/wiki/%D0%92%D0%B8%D0%BA%D0%B8%D0%BD%D0%BE%D0%B2%D0%BE%D1%81%D1%82%D0%B8:',
        
                # Викиданные
                '/wiki/d:',
                '/wiki/Q',
                '/wiki/P',
                
                # Другие языки (примеры из списка)
                '/wiki/%D0%90%D0%B7%C9%99%D1%80%D0%B1%D0%B0%D1%98%D1%81%D0%B0%D0%BD%D0%B4%D0%B6%D0%B0:',
                '/wiki/%D0%91%D0%B0%D1%88%D2%A1%D0%BE%D1%80%D1%82%D1%81%D0%B0:',
                '/wiki/%D0%91%D0%B5%D0%BB%D0%B0%D1%80%D1%83%D1%81%D0%BA%D0%B0%D1%8F:',
                '/wiki/%D0%9D%D0%BE%D1%85%D1%87%D0%B8%D0%B9%D0%BD:',
                '/wiki/%D0%A7%D3%90%D0%B2%D0%B0%D1%88%D0%BB%D0%B0:',
                '/wiki/Hawai%CA%BBi:',
                '/wiki/%D0%93%D0%B0%D0%B9%D0%B5%D1%80%D0%B5%D0%BD:',
                '/wiki/%D0%9A%D1%8B%D1%80%D0%B3%D1%8B%D0%B7%D1%87%D0%B0:',
                '/wiki/%D0%98%D1%80%D0%BE%D0%BD:',
            ]
            
            for pattern in ru_disallowed_patterns:
                if pattern in url or pattern in decoded_url:
                    return True
            
            # Проверяем служебные страницы Википедии (начинающиеся с "Википедия:")
            if '/wiki/Википедия:' in decoded_url or '/wiki/%D0%92%D0%B8%D0%BA%D0%B8%D0%BF%D0%B5%D0%B4%D0%B8%D1%8F:' in url:
                # Но разрешаем некоторые страницы категорий и списков
                allowed_wiki_pages = [
                    '/wiki/Категория:',
                    '/wiki/Список:',
                ]
                for allowed in allowed_wiki_pages:
                    if allowed in decoded_url:
                        return False
                return True
        
        return False
    
    def _crawl_page(self, url: str, source_name: str, depth: int = 0):
        """
        Обкачивает одну страницу.
        
        Args:
            url: URL страницы
            source_name: Название источника
            depth: Текущая глубина обхода
        """
        try:
            normalized_url = URLCrawler.normalize_url(url)
            
            # Проверяем, не превышена ли глубина
            max_depth = self.config['logic'].get('max_depth', 10)
            if depth > max_depth:
                return
            
            # Проверяем, не обработан ли уже этот URL
            if normalized_url in self.visited_urls:
                if not self._should_recheck(normalized_url):
                    return
            
            print(f"[Глубина {depth}] Обкачка: {normalized_url}")
            
            # Загружаем страницу
            response = self._fetch_page(normalized_url)
            if not response:
                print(f"Не удалось загрузить страницу: {normalized_url}. Продолжаем работу...")
                # Помечаем как посещенный, чтобы не пытаться снова сразу
                self.visited_urls.add(normalized_url)
                return
            
            html = response.text
            
            if not html or len(html) < 100:
                print(f"Предупреждение: получен пустой или очень короткий HTML для {normalized_url}")
            
            # Сохраняем документ
            if self._save_document(normalized_url, html, source_name):
                self.visited_urls.add(normalized_url)
                print(f"✓ Успешно сохранен: {normalized_url} ({len(html)} байт)")
            else:
                print(f"✗ Не удалось сохранить: {normalized_url}")
            
            # Извлекаем ссылки для дальнейшей обкачки
            if depth < max_depth:
                links = self._extract_links(html, normalized_url)
                base_domain = URLCrawler.get_domain(normalized_url)
                new_links_count = 0
                
                for link in links:
                    # Добавляем только ссылки с того же домена
                    if self._is_same_domain(link, normalized_url):
                        # Проверяем, не запрещен ли путь для Википедии
                        if self._is_wikipedia_disallowed_path(link):
                            continue  # Пропускаем запрещенные пути
                        
                        if link not in self.visited_urls or self._should_recheck(link):
                            self.url_queue.append({
                                "url": link,
                                "source_name": source_name,
                                "depth": depth + 1
                            })
                            new_links_count += 1
                
                if new_links_count > 0:
                    print(f"  → Найдено {new_links_count} новых ссылок для обкачки")
        except Exception as e:
            print(f"Ошибка при обкачке страницы {url}: {e}")
            import traceback
            traceback.print_exc()
            # Продолжаем работу даже при ошибке
    
    def _extract_urls_from_saved_docs(self, source_name: str = None, max_depth: int = None):
        """
        Извлекает ссылки из уже сохраненных документов для продолжения обкачки.
        
        Args:
            source_name: Имя источника для фильтрации (None = все источники)
            max_depth: Максимальная глубина для извлечения ссылок
        """
        if max_depth is None:
            max_depth = self.config['logic'].get('max_depth', 10)
        
        query = {}
        if source_name:
            query['source_name'] = source_name
        
        print("Извлечение ссылок из сохраненных документов...")
        extracted_count = 0
        processed_count = 0
        queue_urls = {item['url'] for item in self.url_queue}  # Для быстрой проверки дубликатов
        
        try:
            cursor = self.db_collection.find(query, {"url": 1, "html_content": 1})
            total_docs = self.db_collection.count_documents(query)
            
            for doc in cursor:
                url = doc.get('url')
                html = doc.get('html_content', '')
                
                if not html:
                    continue
                
                processed_count += 1
                if processed_count % 100 == 0:
                    print(f"Обработано {processed_count}/{total_docs} документов, найдено {extracted_count} новых ссылок...")
                
                try:
                    links = self._extract_links(html, url)
                    base_domain = URLCrawler.get_domain(url)
                    
                    for link in links:
                        # Добавляем только ссылки с того же домена
                        if self._is_same_domain(link, url):
                            normalized_link = URLCrawler.normalize_url(link)
                            
                            # Проверяем, не запрещен ли путь для Википедии
                            if self._is_wikipedia_disallowed_path(normalized_link):
                                continue  # Пропускаем запрещенные пути
                            
                            # Добавляем в очередь, если еще не обработан или нужно переобкачать
                            if (normalized_link not in self.visited_urls or self._should_recheck(normalized_link)) and \
                               normalized_link not in queue_urls:
                                # Определяем глубину на основе источника
                                depth = 1  # Ссылки из сохраненных документов имеют глубину >= 1
                                self.url_queue.append({
                                    "url": normalized_link,
                                    "source_name": source_name or doc.get('source_name', 'Unknown'),
                                    "depth": depth
                                })
                                queue_urls.add(normalized_link)  # Добавляем в множество для быстрой проверки
                                extracted_count += 1
                except Exception as e:
                    print(f"Ошибка при извлечении ссылок из {url}: {e}")
            
            print(f"Обработано {processed_count} документов")
            if extracted_count > 0:
                print(f"Извлечено {extracted_count} новых ссылок из сохраненных документов")
            else:
                print("Новых ссылок не найдено в сохраненных документах")
        except Exception as e:
            print(f"Ошибка при извлечении ссылок из сохраненных документов: {e}")
    
    def _initialize_queue(self):
        """Инициализирует очередь URL из конфигурации и сохраненных документов."""
        sources = self.config['logic'].get('sources', [])
        
        # Добавляем начальные URL из конфигурации
        for source in sources:
            if source.get('enabled', True):
                url = source['url']
                name = source.get('name', 'Unknown')
                normalized = URLCrawler.normalize_url(url)
                if normalized not in self.visited_urls or self._should_recheck(normalized):
                    self.url_queue.append({
                        "url": normalized,
                        "source_name": name,
                        "depth": 0
                    })
        
        # Извлекаем ссылки из уже сохраненных документов для продолжения обкачки
        # Это позволяет продолжить работу после остановки
        restore_queue = self.config['logic'].get('restore_queue_from_saved', True)
        if restore_queue and len(self.visited_urls) > 0:
            print("\nПопытка восстановить очередь из сохраненных документов...")
            print("(Это может занять некоторое время при большом количестве документов)")
            for source in sources:
                if source.get('enabled', True):
                    source_name = source.get('name', 'Unknown')
                    self._extract_urls_from_saved_docs(source_name=source_name)
    
    def run(self):
        """Запускает процесс обкачки."""
        print("=" * 60)
        print("Запуск поискового робота...")
        print("=" * 60)
        
        # Инициализируем заголовки для сессии
        self.session.headers.update(self._get_headers())
        
        # Подключаемся к базе данных
        self._connect_db()
        
        # Загружаем уже обработанные URL
        self._load_visited_urls()
        
        # Инициализируем очередь
        self._initialize_queue()
        
        default_delay = self.config['logic'].get('delay_between_requests', 5.0)
        max_pages = self.config['logic'].get('max_pages', 0)
        pages_crawled = 0
        pages_saved = 0
        start_time = time.time()
        last_stats_time = start_time
        
        print(f"\nСтатистика:")
        print(f"  - Уже обработано URL: {len(self.visited_urls)}")
        print(f"  - URL в очереди: {len(self.url_queue)}")
        print(f"  - Задержка по умолчанию: {default_delay} сек")
        print(f"  - Задержки из robots.txt будут применяться автоматически")
        print(f"  - Максимальная глубина: {self.config['logic'].get('max_depth', 10)}")
        if max_pages > 0:
            print(f"  - Лимит страниц: {max_pages}")
        print("\nНачинаем обкачку...\n")
        
        try:
            while self.url_queue:
                # Проверяем лимит страниц
                if max_pages > 0 and pages_crawled >= max_pages:
                    print(f"\nДостигнут лимит страниц: {max_pages}")
                    break
                
                # Берем следующий URL из очереди
                try:
                    item = self.url_queue.pop(0)
                    url = item["url"]
                    source_name = item["source_name"]
                    depth = item["depth"]
                except (IndexError, KeyError) as e:
                    print(f"Ошибка при получении URL из очереди: {e}")
                    break
                
                # Обкачиваем страницу
                try:
                    before_save_count = len(self.visited_urls)
                    self._crawl_page(url, source_name, depth)
                    pages_crawled += 1
                    
                    # Проверяем, был ли документ сохранен
                    if len(self.visited_urls) > before_save_count:
                        pages_saved += 1
                    
                    # Периодически выводим статистику
                    current_time = time.time()
                    if current_time - last_stats_time >= 30:  # Каждые 30 секунд
                        elapsed = current_time - start_time
                        rate = pages_crawled / elapsed if elapsed > 0 else 0
                        print(f"\n[Статистика] Обработано: {pages_crawled}, Сохранено: {pages_saved}, "
                              f"В очереди: {len(self.url_queue)}, "
                              f"Неудачных: {len(self.failed_urls)}, "
                              f"Скорость: {rate:.2f} стр/сек\n")
                        last_stats_time = current_time
                    
                except Exception as e:
                    print(f"Критическая ошибка при обкачке {url}: {e}")
                    import traceback
                    traceback.print_exc()
                    # Продолжаем работу
                    pages_crawled += 1
                
                # Задержка между запросами (используем crawl-delay из robots.txt, если доступен)
                # Гарантируем минимум 5 секунд для предотвращения блокировок
                if self.url_queue:  # Не ждем после последнего запроса
                    # Получаем задержку для текущего домена из robots.txt
                    domain_delay = self._get_crawl_delay(url)
                    MIN_DELAY = 5.0  # Минимальная задержка в секундах
                    actual_delay = max(domain_delay, MIN_DELAY)
                    if actual_delay > 0:
                        time.sleep(actual_delay)
            
            # Финальная статистика
            elapsed_time = time.time() - start_time
            print("\n" + "=" * 60)
            print("Обкачка завершена!")
            print("=" * 60)
            print(f"Обработано страниц: {pages_crawled}")
            print(f"Сохранено документов: {pages_saved}")
            print(f"Неудачных загрузок: {len(self.failed_urls)}")
            print(f"Осталось в очереди: {len(self.url_queue)}")
            print(f"Время работы: {elapsed_time:.2f} сек ({elapsed_time/60:.2f} мин)")
            if pages_crawled > 0:
                print(f"Средняя скорость: {pages_crawled/elapsed_time:.2f} стр/сек")
        
        except KeyboardInterrupt:
            elapsed_time = time.time() - start_time
            print("\n\n" + "=" * 60)
            print("Получен сигнал остановки. Робот остановлен.")
            print("=" * 60)
            print(f"Обработано страниц: {pages_crawled}")
            print(f"Сохранено документов: {pages_saved}")
            print(f"Неудачных загрузок: {len(self.failed_urls)}")
            print(f"Осталось в очереди: {len(self.url_queue)}")
            print(f"Время работы: {elapsed_time:.2f} сек")
            print("\nПри следующем запуске робот продолжит с оставшихся URL")
        
        except Exception as e:
            print(f"\nКритическая ошибка: {e}")
            import traceback
            traceback.print_exc()
        
        finally:
            if self.session:
                self.session.close()
            if self.db_client:
                self.db_client.close()
                print("\nСоединение с базой данных закрыто")


def main():
    """Точка входа в программу."""
    if len(sys.argv) != 2:
        print("Использование: python crawler.py <путь_к_config.yaml>")
        sys.exit(1)
    
    config_path = sys.argv[1]
    
    if not os.path.exists(config_path):
        print(f"Ошибка: файл конфигурации '{config_path}' не найден")
        sys.exit(1)
    
    crawler = DocumentCrawler(config_path)
    crawler.run()


if __name__ == "__main__":
    main()

