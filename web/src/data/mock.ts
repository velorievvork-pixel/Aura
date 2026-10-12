import type { Contact, Person, Venue } from '../lib/types';

const WEEKDAYS = [1, 2, 3, 4, 5];

export function makeMe(name: string): Person {
  const first = name.trim().split(/\s+/)[0] || 'Вы';
  return {
    id: 'me',
    name: first,
    nameGen: genitive(first),
    diet: [],
    home: { x: 2, y: 3 },
    busy: [
      { days: WEEKDAYS, from: 10, to: 19, label: 'Работа' },
      { days: [6], from: 9, to: 13, label: 'Спортзал и дела' },
    ],
  };
}

/** Rough Russian genitive for a first name — good enough for "Аура Алекса". */
export function genitive(name: string): string {
  if (/[ая]$/i.test(name)) return name.slice(0, -1) + (/[гкхжчшщ]а$/i.test(name) || /я$/i.test(name) ? 'и' : 'ы');
  if (/й$/i.test(name)) return name.slice(0, -1) + 'я';
  if (/ь$/i.test(name)) return name.slice(0, -1) + 'я';
  if (/[бвгджзклмнпрстфхцчшщ]$/i.test(name)) return name + 'а';
  return name;
}

export const CONTACTS: Contact[] = [
  {
    id: 'dima',
    name: 'Дима',
    nameGen: 'Димы',
    fullName: 'Дима Волков',
    nameIns: 'Димой',
    stems: ['дим', 'дмитри'],
    diet: ['gluten-free'],
    home: { x: 8, y: 6 },
    busy: [
      { days: WEEKDAYS, from: 9, to: 18, label: 'Работа' },
      { days: [6], from: 10, to: 14, label: 'Тренировка' },
    ],
    hasAura: true,
    presence: 'available',
    hue: 200,
    role: 'Сооснователь, продукт',
    permissions: ['Видеть свободные окна', 'Предлагать встречи', 'Бронировать до 1500 ₽'],
    sharedMeetings: ['Воркшоп по AI-стратегии', 'Дизайн-спринт продукта'],
    favorite: true,
  },
  {
    id: 'maria',
    name: 'Мария',
    nameGen: 'Марии',
    fullName: 'Мария Чен',
    nameIns: 'Марией',
    stems: ['мари', 'маш'],
    diet: ['vegetarian'],
    home: { x: 3, y: 8 },
    busy: [
      { days: WEEKDAYS, from: 11, to: 20, label: 'Студия' },
      { days: [0], from: 12, to: 16, label: 'Семья' },
    ],
    hasAura: true,
    presence: 'away',
    hue: 330,
    role: 'Дизайнер',
    permissions: ['Видеть свободные окна', 'Предлагать встречи'],
    sharedMeetings: ['Дизайн-спринт продукта'],
  },
  {
    id: 'kira',
    name: 'Кира',
    nameGen: 'Киры',
    fullName: 'Кира Орлова',
    nameIns: 'Кирой',
    stems: ['кир'],
    diet: ['vegan'],
    home: { x: 9, y: 1 },
    busy: [
      { days: WEEKDAYS, from: 8, to: 17, label: 'Офис' },
      { days: [6, 0], from: 9, to: 21, label: 'За городом' },
    ],
    hasAura: true,
    presence: 'active',
    hue: 270,
    role: 'Инвестор, angel',
    permissions: ['Видеть свободные окна', 'Предлагать встречи', 'Получать документы'],
    sharedMeetings: ['Демо-день инвесторов'],
    favorite: true,
  },
  {
    id: 'ildar',
    name: 'Ильдар',
    nameGen: 'Ильдара',
    fullName: 'Ильдар Сафин',
    nameIns: 'Ильдаром',
    stems: ['ильдар'],
    diet: ['halal'],
    home: { x: 5, y: 2 },
    busy: [],
    hasAura: false,
    presence: 'offline',
    hue: 30,
    role: 'Юрист',
    permissions: [],
    sharedMeetings: [],
  },
];

export const VENUES: Venue[] = [
  { id: 'green-point', name: 'Green Point', kind: 'Кафе', location: { x: 5, y: 4.5 }, avgCheck: 450, tags: ['gluten-free', 'vegetarian', 'vegan'], opens: 8, closes: 23 },
  { id: 'tsekh', name: 'Цех 85', kind: 'Кофейня', location: { x: 4, y: 3 }, avgCheck: 350, tags: ['vegetarian'], opens: 8, closes: 22 },
  { id: 'brasserie', name: 'Брассери Мост', kind: 'Ресторан', location: { x: 5.5, y: 5 }, avgCheck: 1800, tags: ['gluten-free', 'vegetarian', 'halal'], opens: 12, closes: 24 },
  { id: 'chaihana', name: 'Чайхана Лаунж', kind: 'Ресторан', location: { x: 6, y: 2.5 }, avgCheck: 900, tags: ['halal', 'vegetarian'], opens: 11, closes: 23 },
  { id: 'veg-lab', name: 'Veg Lab', kind: 'Кафе', location: { x: 8.5, y: 2 }, avgCheck: 600, tags: ['vegan', 'vegetarian', 'gluten-free'], opens: 9, closes: 21 },
  { id: 'surf', name: 'Surf Coffee', kind: 'Кофейня', location: { x: 2.5, y: 6 }, avgCheck: 300, tags: [], opens: 7, closes: 22 },
];

export const DIET_LABEL: Record<string, string> = {
  'gluten-free': 'без глютена',
  vegetarian: 'вегетарианское',
  vegan: 'веганское',
  halal: 'халяль',
};
